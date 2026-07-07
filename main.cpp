// hyprmono-wm: native window resize/center/snap/expand dispatchers
// Ported from window-manager.sh (bash + hyprctl) to a compiled Hyprland plugin.
//
// Design note: instead of reaching into Hyprland's internal CWindow/CCompositor
// structs (which change frequently between releases and are explicitly called
// out as fragile in the wiki's Advanced page), every read and mutation here
// goes through HyprlandAPI::invokeHyprctlCommand(), i.e. the same stable path
// `hyprctl` itself uses. This keeps the plugin close in spirit to the original
// bash script (which also just shelled out to `hyprctl`/`jq`), just compiled
// in instead of exec'd, and it should need less maintenance across Hyprland
// version bumps than a hook-based approach would.
//
// Verify HANDLE/SDispatchResult/invokeHyprctlCommand signatures against your
// local src/plugins/PluginAPI.hpp before building -- they are not guaranteed
// perfectly stable across Hyprland releases (the wiki says as much).

#include <hyprland/src/plugins/PluginAPI.hpp>

#include <algorithm>
#include <regex>
#include <sstream>
#include <string>

static HANDLE PHANDLE = nullptr;

// Populated in PLUGIN_INIT from plugin:hyprmono_wm:* config values.
static int* PMINW = nullptr;
static int* PMINH = nullptr;
static int* PMAXW = nullptr;
static int* PMAXH = nullptr;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static int clampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void notify(const std::string& msg, const CHyprColor& color, int ms = 3000) {
    HyprlandAPI::addNotification(PHANDLE, "[hyprmono-wm] " + msg, color, ms);
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
// Dispatchers -- one per window-manager.sh subcommand
// ---------------------------------------------------------------------------

// hyprmono:resizerel <dw> <dh>
static SDispatchResult h_resizeRel(std::string arg) {
    std::istringstream ss(arg);
    int dw = 0, dh = 0;
    ss >> dw >> dh;

    auto win = getActiveWindow();
    if (!win.valid) {
        notifyError("No active window found");
        return {.success = false, .error = "no active window"};
    }

    const int newW = clampInt(win.w + dw, *PMINW, *PMAXW);
    const int newH = clampInt(win.h + dh, *PMINH, *PMAXH);

    dispatch("resizewindowpixel exact " + std::to_string(newW) + " " + std::to_string(newH));
    return {.success = true};
}

// hyprmono:resize <w> <h>
static SDispatchResult h_resizeAbs(std::string arg) {
    std::istringstream ss(arg);
    int w = 0, h = 0;
    ss >> w >> h;

    if (w <= 0 || h <= 0) {
        notifyError("resize requires positive width/height, e.g. 'hyprmono:resize 1024 768'");
        return {.success = false, .error = "invalid dimensions"};
    }

    w = clampInt(w, *PMINW, *PMAXW);
    h = clampInt(h, *PMINH, *PMAXH);

    dispatch("resizewindowpixel exact " + std::to_string(w) + " " + std::to_string(h));
    return {.success = true};
}

// hyprmono:center
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

// hyprmono:expand
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

// hyprmono:snap <tl|tr|bl|br|center> <w> <h>
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

// hyprmono:lunar -- Lunar Client optimization preset
static SDispatchResult h_lunar(std::string) {
    auto win = getActiveWindow();
    if (!win.valid) {
        notifyError("No active window found");
        return {.success = false, .error = "no active window"};
    }

    std::string clsLower = win.cls;
    std::transform(clsLower.begin(), clsLower.end(), clsLower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (clsLower.find("lunar") == std::string::npos && clsLower.find("java") == std::string::npos)
        notify("Active window doesn't look like Lunar Client (class: " + win.cls + ")",
               CHyprColor{1.0, 0.8, 0.2, 1.0});

    dispatch("resizewindowpixel exact 1024 768");
    h_center(""); // re-center after resizing
    notify("Lunar Client optimized to 1024x768", CHyprColor{0.4, 0.9, 0.4, 1.0});
    return {.success = true};
}

// hyprmono:info -- shows a notification instead of piping to notify-send
// (this also fixes the `| notify-send ... "$(cat)"` bug from the .conf file,
// where $(cat) never actually captured the piped output)
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
        HyprlandAPI::addNotification(PHANDLE, "[hyprmono-wm] Mismatched headers! Can't proceed.",
                                      CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hyprmono-wm] Version mismatch");
    }

    // Config values, same role as MIN_WIDTH/MIN_HEIGHT/MAX_WIDTH/MAX_HEIGHT
    // in window-manager.sh. Override in hyprland.conf, e.g.:
    //   plugin:hyprmono_wm:min_width = 300
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprmono_wm:min_width",  SConfigValue{.intValue = 200});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprmono_wm:min_height", SConfigValue{.intValue = 150});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprmono_wm:max_width",  SConfigValue{.intValue = 3840});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprmono_wm:max_height", SConfigValue{.intValue = 2160});

    // Pointers are stable after PLUGIN_INIT returns -- cache them once.
    PMINW = &HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprmono_wm:min_width")->intValue;
    PMINH = &HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprmono_wm:min_height")->intValue;
    PMAXW = &HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprmono_wm:max_width")->intValue;
    PMAXH = &HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprmono_wm:max_height")->intValue;

    HyprlandAPI::addDispatcher(PHANDLE, "hyprmono:resizerel", h_resizeRel);
    HyprlandAPI::addDispatcher(PHANDLE, "hyprmono:resize",    h_resizeAbs);
    HyprlandAPI::addDispatcher(PHANDLE, "hyprmono:center",    h_center);
    HyprlandAPI::addDispatcher(PHANDLE, "hyprmono:expand",    h_expand);
    HyprlandAPI::addDispatcher(PHANDLE, "hyprmono:snap",      h_snap);
    HyprlandAPI::addDispatcher(PHANDLE, "hyprmono:lunar",     h_lunar);
    HyprlandAPI::addDispatcher(PHANDLE, "hyprmono:info",      h_info);

    HyprlandAPI::addNotification(PHANDLE, "[hyprmono-wm] Loaded", CHyprColor{0.4, 0.9, 0.4, 1.0}, 3000);

    return {"hyprmono-wm",
            "Native resize/center/snap/expand dispatchers, ported from window-manager.sh",
            "s0uth09", "1.0"};
}

// Hyprland automatically unregisters dispatchers/config values/hooks on
// unload, so there's nothing that strictly needs cleanup here.
APICALL EXPORT void PLUGIN_EXIT() {}
