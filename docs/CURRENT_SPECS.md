# kalin-wm Current Specifications

> **What kalin-wm is RIGHT NOW.** This document describes the implemented state of the compositor as of the latest commit. Agents should read this before touching code so they know what already exists.

---

## What is kalin-wm?

kalin-wm is a **Wayland compositor** built on **wlroots 0.20**. It began as a fork of [dwl](https://codeberg.org/dwl/dwl) ("dwm for Wayland") and has evolved into a **hybrid infinite-canvas / column-tiling compositor**.

Core idea: windows live on an infinite 2D plane. You navigate with a camera (pan, zoom, follow) rather than swapping between fixed workspaces.

---

## Implemented Architecture

### Layout Paradigm: Column Strip

Windows are tiled and auto-placed in a horizontal strip of columns (Niri-style).

Move the focused window between columns:
- `Ctrl+Left` — move to the column on the left
- `Ctrl+Right` — move to the column on the right

### Viewport System

The viewport is a camera looking at the infinite canvas:

| Property | Implementation |
|----------|----------------|
| Position | `viewport.x`, `viewport.y` (world coordinates) |
| Zoom | `viewport.zoom` (float, 1.0 = 100%) |
| Follow focus | `viewport.follow` — camera pans to keep focused window centered |
| Follow new windows | `viewport.follow_new_windows` — camera pans to newly spawned windows |

Navigation:
- `Super+Shift+Arrows` / `Super+Shift+HJKL` — pan camera
- `Super+equal` / `Super+minus` — zoom in / out
- `Super+BackSpace` — reset view to origin, zoom 1.0
- `Super+Z` — toggle follow focus
- `Super+Shift+Z` — toggle follow new windows

### Focus Navigation: Directional Cone Search

- `Super+Arrow` jumps to the nearest window in that direction.
- Algorithm: 90° search cone centered on the desired direction; widens to 180° if no window is found.
- Distance is Euclidean from window center to window center.
- Works in world coordinates so it behaves correctly regardless of zoom or pan.

See: `docs/obsidian-vault/research/active-design/navigation.md`

### Crop Mode

Interactive window cropping:
- Entered via keybind (default config has a binding).
- Mouse drag selects the crop region.
- `Escape` cancels.
- Uses overlay layer for selection rectangle.

### Persistence

- Window world positions are saved and restored across sessions (JSON state in `code/src/persistence.c`).

### Visual Focus Indicator

- Focused windows display a visible focus ring (configurable thickness via `focusringpx`).

### Off-screen Indicators

- Small edge indicators appear when windows exist outside the viewport (configurable via `offscreen_indicator_*`).

---

## Implemented Protocols

kalin-wm implements **15 Essential** and **18 of 21 Recommended** Wayland protocols.

Key protocols:
- `xdg-shell` — standard toplevel windows
- `layer-shell` — status bars, lock screens, backgrounds
- `ext-session-lock-v1` — screen lockers
- `idle-inhibit` — disable idle monitoring for video players
- `output-management` — monitor configuration
- `pointer-constraints` / `relative-pointer` — games and first-person apps
- `virtual-keyboard` / `virtual-pointer` — remote input
- `XWayland` support (optional, compile-time flag)

Full matrix: `docs/obsidian-vault/research/protocols/protocol-matrix.md`

---

## Current Keybindings (from `code/config/config.def.h`)

| Keybind | Action |
|---------|--------|
| `Super+P` | Run launcher (`wmenu-run`) |
| `Super+Shift+Return` | Terminal (`foot`) |
| `Super+J/K` | Focus next/previous in stack |
| `Super+H/L` | Adjust master factor |
| `Super+Arrows` | Directional focus |
| `Super+Shift+Arrows` | Pan camera |
| `Ctrl+Left/Right` | Move focused window between columns |
| `Super+equal/minus` | Zoom in/out |
| `Super+BackSpace` | Reset viewport |
| `Super+Z` | Toggle follow focus |
| `Super+Shift+Z` | Toggle follow new windows |
| `Super+E` | Toggle fullscreen |
| `Super+Shift+Q` | Quit compositor |

All configuration is compile-time via `code/config/config.h` (dwm style).

---

## Build System

```bash
make          # release build → build/kalin-wm
make debug    # debug build with symbols
make test-unit        # C unit tests (12 tests, no wlroots dep)
make test-integration # shell-based integration tests
make test-tty         # run on a real TTY
```

Dependencies: `wlroots-0.20`, `wayland-server`, `xkbcommon`, `libinput`, `wayland-protocols`, `pkg-config`.

Optional: `libxcb`, `libxcb-wm`, `Xwayland` for XWayland support.

Nix flake available: `nix develop` for reproducible environment.

---

## Source Layout

| File | Purpose |
|------|---------|
| `code/src/dwl.c` | Main compositor logic (~3900 lines, still the monolith) |
| `code/src/client.c` | Client lifecycle, anchoring, column placement (~1500 lines) |
| `code/src/input.c` | Keyboard, mouse, input handling (~690 lines) |
| `code/src/viewport.c` | Viewport pan, zoom, follow (~180 lines) |
| `code/src/crop.c` | Crop mode implementation (~210 lines) |
| `code/src/compositor.c` | Compositor glue (~300 lines) |
| `code/src/main.c` | Entry point (~90 lines) |
| `code/src/layouts/tile.c` | dwl tile layout |
| `code/src/layouts/monocle.c` | dwl monocle layout |
| `code/src/layouts/infinite.c` | Infinite canvas layout |
| `code/src/modules/` | Runtime operation chain (commit_size, layout_world, viewport_ops, etc.) |
| `code/include/kalin.h` | Main header — all structs, globals, function declarations |

---

## Known Issues

A **stability audit** identified **23 tracked issues**: 4 critical, 8 high, 9 medium, 2 low.

Most critical areas:
1. **Crop mode** — division by zero, NULL derefs, double-free
2. **Input handling** — NULL keyboard state, grabc dereference during drag
3. **Layout calculation** — division by zero in tile, infinite recursion risk
4. **Client lifecycle** — memory leaks, race conditions, unchecked returns

**This is the primary blocker for v1.0.** See `ROADMAP.md` Phase 0 for the fix checklist and `docs/obsidian-vault/research/active-design/stability-audit.md` for full technical details.

---

## Testing

| Test | How to run | What it covers |
|------|------------|----------------|
| Unit tests | `make test-unit` | Client creation, column placement, spawn crash scenario, resize safety, focus switching, zoom edge cases |
| Integration | `make test-integration` | Static analysis of binary for crash patterns |
| Manual | `scripts/dev/run-nested-safe.sh` | Recommended nested run for manual testing |
| TTY | `make test-tty` | Real hardware test |

See `tests/README.md` and `docs/MANUAL_TESTING.md`.

---

## Version

Current: **0.8-dev**

MVP is complete. v1.0 is in progress.

---

*This document should be updated when significant features land or architectural decisions change.*
