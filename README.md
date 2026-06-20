# kalin-wm

> A **Wayland compositor** with an **infinite 2D canvas** and **hybrid column tiling**.
>
> Forked from [dwl](https://codeberg.org/dwl/dwl) (dwm for Wayland). kalin-wm keeps the suckless philosophy — compile-time configuration, minimal dependencies, hackable C — and adds a viewport-driven navigation model inspired by Niri and DriftWM.

---

## For Agents Working on This Repo

**BEFORE you write code:**
1. Read **`docs/CURRENT_SPECS.md`** to understand what is already implemented.
2. Read **`ROADMAP.md`** to see what needs to be done and what the current priorities are.
3. Check `docs/obsidian-vault/research/` for design documents, stability audits, and protocol research.

**AFTER you complete work:**
- Update `ROADMAP.md` by changing `[ ]` to `[x]` on the items you finished.
- If you changed architecture or added protocols, update `docs/CURRENT_SPECS.md`.

---

## Core Concepts

### Infinite Canvas

Windows live on an unbounded 2D plane. You navigate with a **camera** rather than switching workspaces:

- **Pan** — move the viewport across the canvas (`Super+Shift+Arrows`)
- **Zoom** — scale the viewport in and out (`Super+equal` / `Super+minus`)
- **Follow** — camera automatically tracks the focused window or new spawns (`Super+Z` / `Super+Shift+Z`)

### Column Layout

Windows are auto-placed in a horizontal strip of columns, Niri-style.

Move the focused window between columns:
- `Ctrl+Left` — move to the column on the left
- `Ctrl+Right` — move to the column on the right

### Directional Focus

`Super+Arrow` jumps to the nearest window in that direction using a cone-search algorithm in world coordinates. It works correctly no matter how far you have panned or zoomed.

### Crop Mode

Interactive window cropping with mouse selection. Enter crop mode, drag to select, press Escape to cancel.

---

## Quick Start

### Dependencies

- `libinput`
- `wayland`
- `wlroots` 0.20 (compiled with the libinput backend)
- `xkbcommon`
- `wayland-protocols` (compile-time)
- `pkg-config` (compile-time)

Optional for XWayland support:
- `libxcb`, `libxcb-wm`, `Xwayland`

### Building

```bash
make              # release build → build/kalin-wm
make debug        # debug build with symbols and assertions
make test-unit    # run C unit tests
```

To enable XWayland, uncomment its flags in `code/config/config.mk`.

### Running

```bash
# Nested in an existing X11 or Wayland session
./scripts/dev/run-nested-safe.sh

# Directly from a VT (add your user to `video` and `input` groups)
./build/kalin-wm

# With a startup command (like .xinitrc)
./build/kalin-wm -s 'foot --server'
```

### Configuration

All config is compile-time via `code/config/config.h`, in the dwm tradition. Edit the file, then `make`.

Default keybinds include:
- `Super+P` — run launcher (`wmenu-run`)
- `Super+Shift+Return` — terminal (`foot`)
- `Super+Arrows` — directional focus
- `Super+Shift+Arrows` — pan camera
- `Super+equal/minus` — zoom in/out
- `Super+BackSpace` — reset view
- `Super+E` — toggle fullscreen
- `Super+Shift+Q` — quit

See `code/config/config.def.h` for the full default configuration.

---

## Project Structure

- `code/src/` — source code (compositor, client management, input, viewport, layouts, modules)
- `code/include/` — headers (kalin.h is the main umbrella header)
- `code/config/` — configuration and build flags
- `protocols/` — Wayland protocol XML definitions
- `tests/` — unit tests, integration tests, debug runners
- `scripts/` — development helpers
- `docs/` — documentation, manual testing guide, and the research vault
  - `docs/CURRENT_SPECS.md` — **what is implemented right now**
  - `docs/obsidian-vault/research/` — design docs, stability audits, protocol matrices, comparator research

See `docs/PROJECT_STRUCTURE.md` for the full layout philosophy.

---

## Status

**Version:** 0.8-dev  
**MVP:** ✅ Complete (infinite canvas, pan/zoom, column + anchored windows, crop mode, multi-monitor)  
**v1.0:** 🚧 In progress  

The main blocker for v1.0 is a **stability audit** that identified 23 issues (4 critical, 8 high). These are tracked in `ROADMAP.md` Phase 0 and must be resolved before feature work continues.

Planned v1.0 features include smooth animations, touchpad gestures, window shadows, persistent world state, minimap, bookmarks, and magnetic snapping.

---

## Acknowledgements

kalin-wm is a fork of **dwl** by the dwl developers and community. Many thanks to:

- **Devin J. Pohly** for creating dwl
- The [wlroots](https://gitlab.freedesktop.org/wlroots) and [sway](https://swaywm.org/) developers for the reference implementations
- [suckless.org](https://suckless.org/) and the [dwm](https://dwm.suckless.org/) community for the philosophy
- The [Niri](https://github.com/YaLTeR/niri) and DriftWM projects for layout inspiration

---

*License: GNU GPL v3 (same as dwl)*
