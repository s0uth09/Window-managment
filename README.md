# winctl

A native Hyprland plugin for resizing, centering, snapping, and expanding
the focused window — built against the API documented at
[wiki.hypr.land/Plugins/Development](https://wiki.hypr.land/Plugins/Development/).

No exec/`hyprctl`/`jq` shelling out per keypress: dispatchers are registered
directly with Hyprland and called natively.

**Design choice:** rather than reaching into Hyprland's internal
`CWindow`/`CCompositor` structures (which the wiki's Advanced page explicitly
warns are unstable across releases), every dispatcher here reads and mutates
state through `HyprlandAPI::invokeHyprctlCommand()` — the same path the
`hyprctl` CLI itself uses. Less "deep" than a hook-based approach, but it
should need far less maintenance as Hyprland evolves.

## What's included

| File          | Purpose                                              |
|---------------|-------------------------------------------------------|
| `main.cpp`    | The plugin itself                                     |
| `Makefile`    | Builds `winctl.so` via `pkg-config hyprland`           |
| `hyprpm.toml` | Manifest so `hyprpm` can install/manage this plugin    |
| `install.sh`  | Build + (optionally) install/load helper script        |

## Dispatchers

| Dispatcher          | Args                              | Effect                                    |
|----------------------|-------------------------------------|---------------------------------------------|
| `winctl:resizerel`   | `<dw> <dh>`                        | Resize focused window relative to current size |
| `winctl:resize`      | `<w> <h>`                          | Resize focused window to an absolute size      |
| `winctl:center`      | *(none)*                           | Center focused window on its monitor           |
| `winctl:expand`      | *(none)*                           | Fill the entire focused monitor                |
| `winctl:snap`        | `<tl\|tr\|bl\|br\|center> <w> <h>`  | Snap window to a screen corner/center at a given size |
| `winctl:info`        | *(none)*                           | Show a notification with the focused window's class/title/size/position |

Resize bounds are clamped to fixed constants in `main.cpp`
(`MIN_WIDTH`/`MIN_HEIGHT`/`MAX_WIDTH`/`MAX_HEIGHT`, default 200×150 to
3840×2160) — edit those directly if you want different bounds. (These were
originally meant to be runtime-configurable via `plugin:` config values, but
the current `PluginAPI.hpp` only exposes that through `addConfigValueV2`,
which needs a much heavier `SP<Config::Values::IValue>` type — not worth it
for four clamp bounds.)

## Building

You need Hyprland's development headers. If you installed Hyprland via
`make install` from source, you already have them. Otherwise:

```bash
git clone https://github.com/hyprwm/Hyprland
cd Hyprland
make debug && sudo make installheaders
```

Then, from this directory:

```bash
./install.sh            # builds only, prints next steps
./install.sh --test     # builds, then hyprctl-loads it into your CURRENT session
./install.sh --install  # builds, installs to ~/.local/share/winctl/,
                         # and adds a `plugin =` line to hyprland.conf
```

Or manually:

```bash
make
hyprctl plugin load "$(pwd)/winctl.so"
```

**Recommended first time:** test in a nested debug Hyprland session before
loading into your real one — the wiki's own getting-started guide
recommends this, since a plugin that crashes can take the whole compositor
down with it.

## Example keybinds

```
bind = SUPER+CTRL, Right, winctl:resizerel, 50 0
bind = SUPER+CTRL, Left,  winctl:resizerel, -50 0
bind = SUPER+CTRL, Up,    winctl:resizerel, 0 -50
bind = SUPER+CTRL, Down,  winctl:resizerel, 0 50

bind = SUPER+ALT, 1,      winctl:resize, 1024 768
bind = SUPER+ALT, C,      winctl:center
bind = SUPER+ALT, E,      winctl:expand
bind = SUPER+ALT, KP_7,   winctl:snap, tl 960 540
bind = SUPER+ALT, I,      winctl:info
```

## Uninstalling

```bash
hyprctl plugin unload "$INSTALL_PATH/winctl.so"
```

Then remove the `plugin = ...` line from `hyprland.conf` and delete
`~/.local/share/winctl/`.

## Verification notes (read before filing a "doesn't compile" issue)

This was syntax-checked and unit-tested against stub headers mimicking the
real `PluginAPI.hpp` (types/signatures pulled from the current wiki, and
later cross-checked directly against Hyprland's actual current source), not
against a full Hyprland build environment. Specifically verified:

- `main.cpp` compiles cleanly (`-Wall -Wextra -fsyntax-only`) against the
  stub API surface.
- The JSON-scraping regexes (`getActiveWindow`, `getFocusedMonitorSize`)
  were unit-tested against realistic `hyprctl -j activewindow` / `-j
  monitors` sample output, including edge cases (negative multi-monitor
  coordinates, no focused monitor failing closed rather than returning
  garbage).
- `install.sh` was run end-to-end (build → test-load → permanent install)
  against mocked `pkg-config`/`hyprctl`, including confirming the
  `hyprland.conf` line-append is idempotent on a second run.

What this can't verify without your actual Hyprland source/build: the exact
field names/signatures of `SDispatchResult` and
`HyprlandAPI::invokeHyprctlCommand()` in your specific Hyprland version —
these are explicitly called out by the wiki as not perfectly stable across
releases. If `make` fails on a member name or argument count, open
`src/plugins/PluginAPI.hpp` from your own Hyprland source and adjust
`main.cpp` to match.
