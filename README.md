# kalin-wm

[![License: GPLv3](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)
![Language: C](https://img.shields.io/badge/language-C-555555.svg)
![wlroots](https://img.shields.io/badge/wlroots-0.20-orange.svg)
![Wayland compositor](https://img.shields.io/badge/Wayland-compositor-1793d1.svg)

A personal Wayland compositor, forked from [dwl](https://codeberg.org/dwl/dwl) and grown far past it: an **infinite 2D canvas** navigated by a viewport camera instead of fixed workspaces. Windows are free-positioned at persistent world coordinates and linked into a **connection graph** (up to 8 neighbor connections per window, one per compass direction) instead of a tiled/floating layout mode — the graph drives group-drag, directional swap, gap-closing on close, and push-out-of-the-way on growth.

**Part of the kalin-wm stack:** **kalin-wm** (compositor, this repo) · [quickshell](https://github.com/khhhalin/quickshell) (companion shell — bar, overview, docked panels, notifications) · [test-vm](https://github.com/khhhalin/test-vm) (hardware-accurate QEMU/KVM test harness)

## Engineering highlights

- **Wayland protocol implementations from spec**, on top of wlroots'
  lower-level primitives: `ext-session-lock-v1`,
  `wlr-foreign-toplevel-management-unstable-v1`,
  `hyprland-toplevel-export-v1` (live window previews), and read/write
  output control via `wlr-output-management-v1`.
- **A runtime bind DSL** (parser, hot-reload, and a full-coverage
  validator): every action in the registry must be bound or explicitly
  `unbind`, checked at both startup (fatal on a bad config) and live
  reload (soft-fail, keeps the last-known-good config) — a stale config
  can no longer silently drift out of sync with the compositor's own
  evolving action set.
- **A host-driven, hardware-accurate test harness**: the companion
  [test-vm](https://github.com/khhhalin/test-vm) boots this compositor as
  the real session compositor on a virtual GPU and drives it entirely from
  the host over QMP (input) and VNC (screenshot capture) — no nested
  compositor limitations, no guest-side tooling required.
- **A connection-graph window model**: windows are nodes in an up-to-8-way
  directional graph instead of a tiled/floating hierarchy, with the graph
  itself (not just position/size) persisted across restarts.
- **A dated engineering ledger** (`obsidian/ledger.md`) tracking design
  decisions, bugs, and their root causes as the project evolved — not just
  a changelog, a running record of *why*.

## Features

- **Infinite canvas + camera**: pan, zoom, follow-focus, follow-new-windows, fit-all.
- **Connection graph**: directional swap, spliced gap-closing, growth-overlap push, persisted across restarts.
- **Native overview mode** (`Super+O`): fit-all promoted to a toggle with click-to-jump, no separate renderer.
- **Crop mode**: clip a window to a sub-region.
- **Interactive screenshot UI** (`Super+Shift+S`): niri-style region select, save to disk and/or clipboard.
- **Session lock** (`ext-session-lock-v1`): works with any lock client, e.g. `swaylock`.
- **Window docking primitive**: the shell can embed a real, fully-interactive client (not a rendered texture) at a fixed screen rect — used for docked bar panels.
- **Runtime bind DSL**: `~/.config/kalin-wm/binds.conf`, hot-reloaded, with full-coverage validation (every action must be bound or explicitly `unbind`— the compositor refuses to start on a stale/incomplete config).
- **IPC socket**: camera state/commands, window list, per-output settings (read/write via `wlr-output-management`), and brightness control (via logind, not raw sysfs) for TUI/shell clients.
- **Foreign-toplevel export** (`wlr-foreign-toplevel-management-unstable-v1` + `hyprland-toplevel-export-v1`): window list and live previews for the shell.

## Quick start

### Build

```bash
nix develop -c make clean all
make test-unit
```

### Run nested (inside an existing graphical session)

```bash
WLR_BACKENDS=wayland ./build/kalin-wm -d
```

With the Quickshell bar loaded (requires the [quickshell](https://github.com/khhhalin/quickshell) repo checked out alongside this one):

```bash
QS_CONFIG_PATH=../quickshell WLR_BACKENDS=wayland ./build/kalin-wm -d -s 'qs & foot --server'
```

### Test with the real QEMU/KVM VM

The trustworthy end-to-end test is [test-vm](https://github.com/khhhalin/test-vm): it boots kalin-wm as the actual session compositor on a virtual DRM/GL GPU, driven entirely from the host (input injection + screenshot capture over QMP/VNC, no guest-side tooling). See that repo's README.

### Run on a TTY / as a login session

```bash
./scripts/run-tty [secs]      # foreground on the current VT, logs to /tmp
./scripts/test-tty3 [secs]    # automated: switches to VT3, captures logs, switches back
```

To make it available as a login-manager session entry, install the package and write a `.desktop` file under `/etc/wayland-sessions/` (see `docs/desktop/` for the template) — see your distro's session-management docs for the specifics.

## Default keybindings

`Super` is the Windows/Command key. This table covers the shipped defaults (`code/config/default_binds.h`, written to `~/.config/kalin-wm/binds.conf` on first run only); edit that file to customize — see `obsidian/keybindings.md` for the full bind DSL grammar.

| Key | Action |
|-----|--------|
| `Super+T` | Terminal (`foot`) |
| `Super+P` | Launcher (`fuzzel`) |
| `Super+O` | Toggle overview mode |
| `Super+Q` | Close focused window |
| `Super+C` | Crop mode (`Super+Shift+R` to cancel) |
| `Super+E` | Toggle fullscreen |
| `Super+F` | Fit width (grows/shrinks evenly from center) |
| `Super+Shift+F` | Fit height |
| `Super+M` | Toggle maximized |
| `Super+Shift+T` | Toggle always-on-top |
| `Super+Shift+O` | Toggle overlap (let a window overlap its graph neighbors) |
| `Super+L` | Link-pick: arm a connection source, click another window to link |
| `Super+N` | Toggle minimized |
| `Super+grave` | Toggle scratchpad terminal |
| `Super+Arrows` | Directional focus (cone search) |
| `Super+Ctrl+Arrows` | Swap focused window with its graph neighbor in that direction |
| `Super+J` / `Super+K` | Cycle focus through the window stack |
| `Super+Shift+Arrows` / `Super+Shift+HJKL` | Pan camera |
| `Super+Ctrl+equal` / `Super+Ctrl+minus` | Zoom camera |
| `Super+0` | Fit all windows |
| `Super+BackSpace` | Reset camera |
| `Super+Z` / `Super+Shift+Z` | Toggle follow mode / follow-new-windows |
| `Super+equal`/`minus`, `Super+bracketleft`/`bracketright` | Narrow / widen focused window |
| `Super+Shift+plus`/`underscore` | Shorten / lengthen focused window |
| `Super+Shift+S` | Interactive screenshot UI |
| `Super+Print` | Screenshot (whole monitor, immediate) |
| `Super+V` | Clipboard history picker |
| `Super+comma` / `Super+period` | Focus monitor left / right |
| `tap Super` | Toggle launcher |
| `hold Super` | Window-action menu |
| `Super+Escape` | Quit the compositor |

## Project layout

- `code/src/dwl.c` + `code/src/modules/` — compositor source (dwl.c is the shrinking core; most features live in `modules/`: `viewport/`, `layout/`, `crop/`, `input/`, `ui/`, `screenshot/`, `protocols/`, `binds/`, plus `ipc.c`, `foreign_toplevel.c`, `backlight.c`, `capture.c`, `session_lock.c`, `persistence.c`, `crash_report.c`)
- `code/include/` — headers (`kalin.h` is the shared umbrella for the modules)
- `code/config/` — compile-time config + the default bind DSL
- `scripts/` — development helpers
- `code/tests/` — unit tests (`make test-unit`)
- `obsidian/` — design vault: goal note, ledger (dated decision log), and one note per subsystem — the actual source of truth for how this project works, more current than this README
- `.claude/skills/kalin-wm/` — a [Claude Code](https://claude.com/claude-code) skill with the build/test/run/VM workflow, if you use Claude Code on this repo

## Status

**Version:** 0.8-dev — MVP complete, v1.0 in progress. See `obsidian/roadmap.md` for open work and `obsidian/ledger.md` for the dated history of how it got here.

---

*License: GNU GPL v3 (same as dwl)*
