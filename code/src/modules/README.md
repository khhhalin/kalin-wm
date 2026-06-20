# Runtime Modules (src/modules)

This folder defines a runtime operation chain for the compositor's world/crop/view logic.

Execution order in [dwl.c](../../dwl.c):

1. `crop/crop_mode.c`  
   User interaction for crop mode (enter/draw/apply/cancel).
2. `layout/layout_world.c`  
   Infinite world layout, column flow, and anchoring behavior.
3. `ui/overlay_clock.c`
   Lightweight compositor-side HH:MM overlay clock (bottom-right).
4. `ui/wallpaper.c`  
   World-anchored repeating wallpaper generation and placement.
5. `viewport/viewport_ops.c`  
   Camera movement, zoom, follow, and smoothing.
6. `input/resize_actions.c`  
   Keyboard-driven focused-window resize actions.
7. `ui/offscreen_indicators.c`
   Edge indicators for off-screen windows.

## Why this exists

The previous mixed file made it hard to reason about flow.  
This chain improves readability while preserving a single translation unit model (currently included from `dwl.c`).

Legacy module files in this directory are deprecated in favor of the subfolders above.

## Target architecture

This directory is the staging area for a fully normal C module layout:

- `.c` files compiled as separate translation units
- explicit `.h` interfaces in `include/`
- no `.c` includes from `dwl.c`

Current milestone:

- `input/commit_size.c` is compiled as a standalone translation unit (first migrated runtime module).

## Invariants

- No external linkage is introduced; functions remain in the same compilation model.
- Layout and wallpaper stay world-space aligned.
- Viewport updates continue to drive arrangement and rendering behavior.
