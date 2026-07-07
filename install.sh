#!/usr/bin/env bash
# install.sh -- build (and optionally install) the hyprmono-wm plugin
#
# Usage:
#   ./install.sh              build only, print next-step instructions
#   ./install.sh --test       build, then hyprctl-load it into your CURRENT
#                             Hyprland session for testing (unloads on logout,
#                             does not touch hyprland.conf)
#   ./install.sh --install    build, copy the .so to ~/.local/share/hyprmono-wm/,
#                             and add a `plugin = ...` line to hyprland.conf
#                             (idempotent -- won't add the line twice)
#
# Per wiki.hypr.land/Plugins/Development/Getting-Started, it's strongly
# recommended to test plugins in a nested Hyprland session before loading
# them into your real one -- a bad plugin can crash your whole compositor.
# This script does not start a nested session for you; do that yourself with
# a debug build if you want maximum safety, especially the first time.

set -uo pipefail  # deliberately NOT -e -- see note below

log_info()    { echo -e "\033[1;34m[INFO]\033[0m $*"; }
log_success() { echo -e "\033[1;32m[ OK ]\033[0m $*"; }
log_warn()    { echo -e "\033[1;33m[WARN]\033[0m $*"; }
log_error()   { echo -e "\033[1;31m[FAIL]\033[0m $*" >&2; }

# Resolve this script's own directory, so it works regardless of cwd
# (see the discussion earlier in this repo about install-window-manager.sh's
# ./relative-path bug -- same fix applied here from the start).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_SO="$SCRIPT_DIR/hyprmono-wm.so"
INSTALL_DIR="$HOME/.local/share/hyprmono-wm"
HYPR_CONF="$HOME/.config/hypr/hyprland.conf"

MODE="build"
case "${1:-}" in
    --test)    MODE="test" ;;
    --install) MODE="install" ;;
    "" )       MODE="build" ;;
    *)
        log_error "Unknown argument: $1 (expected --test, --install, or nothing)"
        exit 1
        ;;
esac

# --- Dependency checks -------------------------------------------------
# NOTE: no `set -e` above, on purpose: it masks failures of `local x=$(cmd)`
# style assignments (a bug we found and fixed elsewhere in this repo's
# window-manager.sh). Every command below is checked explicitly instead.

missing=0
for dep in g++ make pkg-config hyprctl; do
    if ! command -v "$dep" >/dev/null 2>&1; then
        log_error "Required dependency '$dep' not found in PATH"
        missing=1
    fi
done
if [[ "$missing" -eq 1 ]]; then
    log_error "Install the missing dependencies and re-run this script"
    exit 1
fi

if ! pkg-config --exists hyprland; then
    log_error "pkg-config can't find 'hyprland'."
    log_error "This means Hyprland's development headers aren't installed."
    log_error "Fix: clone Hyprland's source and run:"
    log_error "  cd Hyprland && make debug && sudo make installheaders"
    log_error "(see https://wiki.hypr.land/Plugins/Development/Getting-Started/)"
    exit 1
fi
log_success "Found hyprland pkg-config file: $(pkg-config --modversion hyprland 2>/dev/null || echo unknown version)"

# --- Build ---------------------------------------------------------------
log_info "Building hyprmono-wm.so..."
if ! make -C "$SCRIPT_DIR" clean all; then
    log_error "Build failed. If this is a C++ standard mismatch, edit"
    log_error "CXXSTD in Makefile to match the standard your Hyprland build uses."
    exit 1
fi

if [[ ! -f "$PLUGIN_SO" ]]; then
    log_error "Build reported success but $PLUGIN_SO is missing -- aborting"
    exit 1
fi
log_success "Built $PLUGIN_SO"

# --- Mode: build only ------------------------------------------------------
if [[ "$MODE" == "build" ]]; then
    cat <<EOF

Next steps:
  Test in your CURRENT session (not persistent, safe-ish to try):
    hyprctl plugin load "$PLUGIN_SO"
    hyprctl plugin unload "$PLUGIN_SO"   # to remove it again

  Or re-run this script with a mode:
    $0 --test       load it now for this session only
    $0 --install    install it permanently (adds a line to hyprland.conf)
EOF
    exit 0
fi

# --- Mode: test-load into current session --------------------------------
if [[ "$MODE" == "test" ]]; then
    log_info "Loading plugin into the current Hyprland session..."
    if ! hyprctl plugin load "$PLUGIN_SO"; then
        log_error "hyprctl plugin load failed -- check 'hyprctl plugin list' and your Hyprland log"
        exit 1
    fi
    log_success "Loaded. Try: hyprctl dispatch hyprmono:info"
    log_info "This is NOT persisted across restarts. Use --install for that."
    exit 0
fi

# --- Mode: permanent install -----------------------------------------------
if [[ "$MODE" == "install" ]]; then
    mkdir -p "$INSTALL_DIR"
    cp "$PLUGIN_SO" "$INSTALL_DIR/hyprmono-wm.so"
    log_success "Copied plugin to $INSTALL_DIR/hyprmono-wm.so"

    PLUGIN_LINE="plugin = $INSTALL_DIR/hyprmono-wm.so"

    if [[ ! -f "$HYPR_CONF" ]]; then
        log_error "$HYPR_CONF not found -- create it first, or add this line yourself:"
        log_error "  $PLUGIN_LINE"
        exit 1
    fi

    if grep -qxF "$PLUGIN_LINE" "$HYPR_CONF"; then
        log_info "hyprland.conf already references this plugin, not adding a duplicate line"
    else
        {
            echo ""
            echo "# hyprmono-wm: native resize/center/snap/expand dispatchers"
            echo "$PLUGIN_LINE"
        } >> "$HYPR_CONF"
        log_success "Added plugin line to $HYPR_CONF"
    fi

    log_info "Run 'hyprctl reload' to load it now, or restart Hyprland."
    log_warn "First time installing a new plugin? Consider testing with --test"
    log_warn "in a nested session first -- a crashing plugin can take down your session."
    exit 0
fi
