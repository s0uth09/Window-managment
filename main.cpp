// winctl: native window resize/center/snap/expand dispatchers for Hyprland
//
// Design note: instead of reaching into Hyprland's internal CWindow/CCompositor
// structs (which change frequently between releases and are explicitly called
// out as fragile in the wiki's Advanced page), every read and mutation here
// goes through HyprlandAPI::invokeHyprctlCommand(), i.e. the same stable path
// `hyprctl` itself uses. That should need less maintenance across Hyprland
// version bumps than a hook-based approach would.
//
// Verify HANDLE/SDispatchResult/invokeHyprctlCommand signatures against your
// local src/plugins/PluginAPI.hpp before building -- they are not guaranteed
// perfectly stable across Hyprland releases (the wiki says as much).

#include <hyprland/src/plugins/PluginAPI.hpp>

#include <regex>
#include <sstream>
#include <string>

static HANDLE PHANDLE = nullptr;

// Resize bounds. These were originally meant to be user-configurable via
// HyprlandAPI::addConfigValue(), but that API takes/returns SConfigValue,
// which is only forward-declared (not a complete type) in the current
// PluginAPI.hpp -- it can't be constructed here without pulling in a lot
// more of Hyprland's config machinery (Config::Values::IValue via
// addConfigValueV2). Given these are just clamp bounds, plain constants
// are simpler and avoid depending on an API surface that's still shifting.
// Edit these directly if you want different bounds.
static constexpr int MIN_WIDTH  = 200;
static constexpr int MIN_HEIGHT = 150;
static constexpr int MAX_WIDTH  = 3840;
static constexpr int MAX_HEIGHT = 2160;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static int clampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void notify(const std::string& msg, const CHyprColor& color, int ms = 3000) {
    HyprlandAPI::addNotification(PHANDLE, "[winctl] " + msg, color, ms);
}

static void notifyError(const std::string& msg) {
    notify(msg, CHyprColor{1.0, 0.3, 0.3, 1.0});
}

// Fire a dispatch through hyprctl's own dispatch path -- same effect as
// `hyprctl dispatch <name> <args>` on the CLI.
static void dispatch(const std::string& nameAndArgs) {
    HyprlandAPI::invokeHyprctlCommand("dispatch", nameAndArgs);
}

struct SActiveWindow {
    bool     valid = false;
    int      x = 0, y = 0, w = 0, h = 0;
    std::string cls, title;
};

// Equivalent of the bash script's `get_active_window` + jq field pulls.
// Parses the JSON returned by `hyprctl -j activewindow` with targeted
// regexes rather than a full JSON parser, to keep this file dependency-free.
static SActiveWindow getActiveWindow() {
    SActiveWindow info;
    const std::string json = HyprlandAPI::invokeHyprctlCommand("activewindow", "", "j");

    if (json.empty() || json.find("\"class\"") == std::string::npos)
        return info; // no focused window

    auto extractPair = [&](const char* key, int& a, int& b) {
        std::regex re(std::string("\"") + key + "\"\\s*:\\s*\\[\\s*(-?\\d+)\\s*,\\s*(-?\\d+)");
        std::smatch m;
        if (std::regex_search(json, m, re)) {
            a = std::stoi(m[1].str());
            b = std::stoi(m[2].str());
        }
    };
    auto extractStr = [&](const char* key) -> std::string {
        std::regex re(std::string("\"") + key + "\"\\s*:\\s*\"([^\"]*)\"");
        std::smatch m;
        if (std::regex_search(json, m, re))
            return m[1].str();
        return "";
    };

    extractPair("size", info.w, info.h);
    extractPair("at", info.x, info.y);
    info.cls   = extractStr("class");
    info.title = extractStr("title");
    info.valid = true;
    return info;
}

// Equivalent of the bash script's monitor width/height lookup.
static bool getFocusedMonitorSize(int& w, int& h) {
    const std::string json = HyprlandAPI::invokeHyprctlCommand("monitors", "", "j");

    std::regex reObj(R"(\{[^{}]*"focused"\s*:\s*true[^{}]*\})");
    std::smatch m;
    if (!std::regex_search(json, m, reObj))
        return false;

    const std::string obj = m[0].str();
    std::regex reW("\"width\"\\s*:\\s*(\\d+)");
    std::regex reH("\"height\"\\s*:\\s*(\\d+)");
    std::smatch mw, mh;
    if (!std::regex_search(obj, mw, reW) || !std::regex_search(obj, mh, reH))
        return false;

    w = std::stoi(mw[1].str());
    h = std::stoi(mh[1].str());
    return true;
}

// ---------------------------------------------------------------------------
// Dispatchers
// ---------------------------------------------------------------------------

// winctl:resizerel <dw> <dh>
static SDispatchResult h_resizeRel(std::string arg) {
    std::istringstream ss(arg);
    int dw = 0, dh = 0;
    ss >> dw >> dh;

    auto win = getActiveWindow();
    if (!win.valid) {
        notifyError("No active window found");
        return {.success = false, .error = "no active window"};
    }

    const int newW = clampInt(win.w + dw, MIN_WIDTH, MAX_WIDTH);
    const int newH = clampInt(win.h + dh, MIN_HEIGHT, MAX_HEIGHT);

    dispatch("resizewindowpixel exact " + std::to_string(newW) + " " + std::to_string(newH));
    return {.success = true};
}

// winctl:resize <w> <h>
static SDispatchResult h_resizeAbs(std::string arg) {
    std::istringstream ss(arg);
    int w = 0, h = 0;
    ss >> w >> h;

    if (w <= 0 || h <= 0) {
        notifyError("resize requires positive width/height, e.g. 'winctl:resize 1024 768'");
        return {.success = false, .error = "invalid dimensions"};
    }

    w = clampInt(w, MIN_WIDTH, MAX_WIDTH);
    h = clampInt(h, MIN_HEIGHT, MAX_HEIGHT);

    dispatch("resizewindowpixel exact " + std::to_string(w) + " " + std::to_string(h));
    return {.success = true};
}

// winctl:center
static SDispatchResult h_center(std::string) {
    auto win = getActiveWindow();
    int mw = 0, mh = 0;
    if (!win.valid || !getFocusedMonitorSize(mw, mh)) {
        notifyError("Could not determine window or monitor geometry");
        return {.success = false, .error = "geometry lookup failed"};
    }

    const int x = (mw - win.w) / 2;
    const int y = (mh - win.h) / 2;

    dispatch("movewindowpixel exact " + std::to_string(x) + " " + std::to_string(y));
    return {.success = true};
}

// winctl:expand
static SDispatchResult h_expand(std::string) {
    int mw = 0, mh = 0;
    if (!getFocusedMonitorSize(mw, mh)) {
        notifyError("Could not determine monitor geometry");
        return {.success = false, .error = "monitor lookup failed"};
    }

    dispatch("resizewindowpixel exact " + std::to_string(mw) + " " + std::to_string(mh));
    dispatch("movewindowpixel exact 0 0");
    return {.success = true};
}

// winctl:snap <tl|tr|bl|br|center> <w> <h>
static SDispatchResult h_snap(std::string arg) {
    std::istringstream ss(arg);
    std::string pos;
    int w = 800, h = 600;
    ss >> pos >> w >> h;

    int mw = 0, mh = 0;
    if (!getFocusedMonitorSize(mw, mh)) {
        notifyError("Could not determine monitor geometry");
        return {.success = false, .error = "monitor lookup failed"};
    }

    int x = 0, y = 0;
    if (pos == "tl" || pos == "top-left") {
        x = 0; y = 0;
    } else if (pos == "tr" || pos == "top-right") {
        x = mw - w; y = 0;
    } else if (pos == "bl" || pos == "bottom-left") {
        x = 0; y = mh - h;
    } else if (pos == "br" || pos == "bottom-right") {
        x = mw - w; y = mh - h;
    } else if (pos == "center") {
        x = (mw - w) / 2; y = (mh - h) / 2;
    } else {
        notifyError("Unknown snap position: '" + pos + "' (use tl/tr/bl/br/center)");
        return {.success = false, .error = "unknown position"};
    }

    dispatch("resizewindowpixel exact " + std::to_string(w) + " " + std::to_string(h));
    dispatch("movewindowpixel exact " + std::to_string(x) + " " + std::to_string(y));
    return {.success = true};
}

// winctl:info -- shows a notification instead of piping to notify-send
// (this also fixes a `| notify-send ... "$(cat)"` pattern bug some configs
// use, where $(cat) never actually captures the piped output, since command
// substitution runs before the pipe connects the two commands)
static SDispatchResult h_info(std::string) {
    auto win = getActiveWindow();
    if (!win.valid) {
        notifyError("No active window found");
        return {.success = false, .error = "no active window"};
    }

    const std::string msg = win.title + " [" + win.cls + "] " +
                             std::to_string(win.w) + "x" + std::to_string(win.h) +
                             " at " + std::to_string(win.x) + "," + std::to_string(win.y);

    notify(msg, CHyprColor{0.7, 0.7, 0.7, 1.0}, 5000);
    return {.success = true};
}

// ---------------------------------------------------------------------------
// Plugin entry points
// ---------------------------------------------------------------------------

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string COMPOSITOR_HASH = __hyprland_api_get_hash();
    const std::string CLIENT_HASH     = __hyprland_api_get_client_hash();

    // ALWAYS check this: prevents crashes from mismatched header versions.
    if (COMPOSITOR_HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[winctl] Mismatched headers! Can't proceed.",
                                      CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[winctl] Version mismatch");
    }

    // Resize bounds are plain constants (see MIN_WIDTH etc. above) rather
    // than user-configurable plugin: config values -- see the comment there
    // for why.

    // addDispatcher is deprecated in current Hyprland in favor of
    // addDispatcherV2, which is what our handlers (returning SDispatchResult)
    // are already written for.
    HyprlandAPI::addDispatcherV2(PHANDLE, "winctl:resizerel", h_resizeRel);
    HyprlandAPI::addDispatcherV2(PHANDLE, "winctl:resize",    h_resizeAbs);
    HyprlandAPI::addDispatcherV2(PHANDLE, "winctl:center",    h_center);
    HyprlandAPI::addDispatcherV2(PHANDLE, "winctl:expand",    h_expand);
    HyprlandAPI::addDispatcherV2(PHANDLE, "winctl:snap",      h_snap);
    HyprlandAPI::addDispatcherV2(PHANDLE, "winctl:info",      h_info);

    HyprlandAPI::addNotification(PHANDLE, "[winctl] Loaded", CHyprColor{0.4, 0.9, 0.4, 1.0}, 3000);

    return {"winctl",
            "Native resize/center/snap/expand window dispatchers for Hyprland",
            "you", "1.0"};
}

// Hyprland automatically unregisters dispatchers/config values/hooks on
// unload, so there's nothing that strictly needs cleanup here.
APICALL EXPORT void PLUGIN_EXIT() {}
