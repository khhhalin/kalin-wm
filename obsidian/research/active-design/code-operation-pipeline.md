# Runtime Code Operation Pipeline

This document defines the readability-oriented runtime flow for kalin-wm.

## Pipeline stages

For world/crop/viewport behavior, execution is organized in this order:

1. **Crop Input Stage**  
   File: `src/modules/crop_mode.c`  
   Responsibilities:
   - crop mode entry/cancel/apply
   - crop overlay visuals
   - post-crop column height correction

2. **World Layout Stage**  
   File: `src/modules/layout_world.c`  
   Responsibilities:
   - infinite world layout
   - column placement/reflow
   - anchored/freeform movement
   - world/screen transform macros

3. **Wallpaper Stage**  
   File: `src/modules/wallpaper.c`  
   Responsibilities:
   - repeating wallpaper generation
   - world-anchored tiling and camera sync

4. **Viewport Stage**  
   File: `src/modules/viewport_ops.c`  
   Responsibilities:
   - camera pan/zoom
   - smooth interpolation (`viewport_tick`)
   - follow modes

## Integration point

`dwl.c` includes the chain in explicit order:

- `#include "src/modules/crop_mode.c"`
- `#include "src/modules/layout_world.c"`
- `#include "src/modules/wallpaper.c"`
- `#include "src/modules/viewport_ops.c"`

This preserves a single translation-unit model while making behavior flow obvious.

## Folder structure intent

- `src/modules/` holds runtime stages.
- Each stage should keep one dominant concern.

## Follow-up refactor targets

To continue improving readability safely, apply the same pattern to:

- input event handling (`keypress`, pointer events)
- client lifecycle (`create/map/unmap/destroy`)
- monitor/output lifecycle
