# Window Managment

A native Hyprland plugin version of `window-manager.sh` — resize, center,
snap, and expand the focused window without shelling out to `hyprctl`/`jq`
on every keypress. Built against the API documented at
[wiki.hypr.land/Plugins/Development](https://wiki.hypr.land/Plugins/Development/).

## Why a plugin instead of the bash script?

The bash version works, but every keybind pays the cost of spawning a shell,
`hyprctl`, and `jq`. A compiled plugin registers real dispatchers that
Hyprland calls directly — same effect, no process spawn per keypress, and
your logic ships as one `.so` instead of three files you have to keep in
sync (`window-manager.sh`, `install-window-manager.sh`,
`windowmanagement.conf`).

**Design choice:** rather than reaching into Hyprland's internal
`CWindow`/`CCompositor` structures (which the wiki's Advanced page explicitly
warns are unstable across releases), every dispatcher here reads and mutates
state through `HyprlandAPI::invokeHyprctlCommand()` — the same path the
`hyprctl` CLI itself uses. It's a little less "native" than a deep hook, but
it should need far less maintenance as Hyprland evolves, and it mirrors what
the original bash script was already doing.

## What's included

| File          | Purpose                                              |
|---------------|-------------------------------------------------------|
| `main.cpp`    | The plugin itself                                     |
| `Makefile`    | Builds `hyprmono-wm.so` via `pkg-config hyprland`      |
| `hyprpm.toml` | Manifest so `hyprpm` can install/manage this plugin    |
| `install.sh`  | Build + (optionally) install/load helper script        |

## Dispatchers

| Dispatcher              | Args                        | Equivalent bash command        |
|--------------------------|------------------------------|----------------------------------|
| `hyprmono:resizerel`     | `<dw> <dh>`                  | `resize_relative`                |
| `hyprmono:resize`        | `<w> <h>`                    | `resize_absolute`                 |
| `hyprmono:center`        | *(none)*                     | `center_window`                   |
| `hyprmono:expand`        | *(none)*                     | `expand_to_screen`                |
| `hyprmono:snap`          | `<tl\|tr\|bl\|br\|center> <w> <h>` | `snap_window`               |
| `hyprmono:lunar`         | *(none)*                     | `optimize_lunar_client`            |
| `hyprmono:info`          | *(none)*                     | `get_window_info` (shown as a notification, not printed) |

All width/height values are clamped to `plugin:hyprmono_wm:min_width` /
`min_height` / `max_width` / `max_height` (defaults: 200×150 to 3840×2160).
Override in `hyprland.conf`:

```
plugin:hyprmono_wm:min_width = 300
plugin:hyprmono_wm:max_height = 1440
```

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
./install.sh          # builds only, prints next steps
./install.sh --test   # builds, then hyprctl-loads it into your CURRENT session
./install.sh --install  # builds, installs to ~/.local/share/hyprmono-wm/,
                         # and adds a `plugin =` line to hyprland.conf
```

Or manually:

```bash
make
hyprctl plugin load "$(pwd)/hyprmono-wm.so"
```

**Recommended first time:** test in a nested debug Hyprland session before
loading into your real one — the wiki's own getting-started guide
recommends this, since a plugin that crashes can take the whole compositor
down with it.

## Example keybinds

Replace your old `windowmanagement.conf` `bind = ... exec, $windowmgr ...`
lines with native dispatcher calls:

```
bind = SUPER+CTRL, Right, hyprmono:resizerel, 50 0
bind = SUPER+CTRL, Left,  hyprmono:resizerel, -50 0
bind = SUPER+CTRL, Up,    hyprmono:resizerel, 0 -50
bind = SUPER+CTRL, Down,  hyprmono:resizerel, 0 50

bind = SUPER+ALT, 1, hyprmono:resize, 1024 768
bind = SUPER+ALT, C, hyprmono:center
bind = SUPER+ALT, E, hyprmono:expand
bind = SUPER+ALT, KP_7, hyprmono:snap, tl 960 540
bind = SUPER+ALT, L, hyprmono:lunar
bind = SUPER+ALT, I, hyprmono:info
```

Note: this also fixes a real bug in the old config's info keybind
(`$windowmgr info | notify-send -t 5000 "$(cat)"`) — `$(cat)` never actually
captured the piped `window-manager.sh info` output, since command
substitution runs before the pipe connects the two commands. `hyprmono:info`
just shows the notification directly.

## Uninstalling

```bash
hyprctl plugin unload "$INSTALL_PATH/hyprmono-wm.so"
```

Then remove the `plugin = ...` line from `hyprland.conf` and delete
`~/.local/share/hyprmono-wm/`.

## Verification notes (read before filing a "doesn't compile" issue)

This was syntax-checked and unit-tested in a sandbox against **stub headers
that mimic** the real `PluginAPI.hpp` (types/signatures pulled from the
current wiki and cross-referenced against real plugins like `hyprexpo`) —
not against Hyprland's actual source tree, which isn't available in the
environment this was built in. Specifically verified:

- `main.cpp` compiles cleanly (`-Wall -Wextra -fsyntax-only`) against the
  stub API surface — no type errors, no missing includes, no unbalanced
  braces.
- The JSON-scraping regexes (`getActiveWindow`, `getFocusedMonitorSize`)
  were unit-tested against realistic `hyprctl -j activewindow` / `-j
  monitors` sample output, including edge cases: negative window
  coordinates (common in multi-monitor setups) and "no focused monitor"
  failing closed instead of returning garbage.
- `install.sh` was run end-to-end (build → test-load → permanent install)
  against mocked `pkg-config`/`hyprctl`, including confirming the
  `hyprland.conf` line-append is idempotent on a second run.

What this **can't** verify without your actual Hyprland source/build:
- The exact field names/signature of `SDispatchResult`, `SConfigValue`, and
  `HyprlandAPI::invokeHyprctlCommand()` in your specific Hyprland version —
  these are explicitly called out by the wiki as not perfectly stable across
  releases. If `make` fails on a member name or argument count, open
  `src/plugins/PluginAPI.hpp` from your own Hyprland source and adjust
  `main.cpp` to match — the logic (what to compute, when to dispatch) won't
  need to change, just the exact API surface it's called through.
- Runtime behavior against a real compositor (no Hyprland instance was
  available to actually load and exercise the plugin against).
