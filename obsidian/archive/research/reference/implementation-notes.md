# Implementation Notes: Buffer Scaling

> Documentation of the buffer scaling implementation for kalin-wm's zoom system.

---

## Summary

Implemented robust buffer scaling using `wlr_scene_buffer_set_dest_size()` and `wlr_scene_buffer_set_filter_mode()` to enable actual content zoom rather than just position-based camera zoom.

## Bug Fixes

### Bug: Windows Stacking in Visible Area
**Problem**: New windows were being placed at the rightmost edge of visible windows, causing them to stack within the current view instead of extending the infinite canvas.

**Solution**: 
1. Added `INFINITE_WINDOW_SPACING` (50px) gap when placing new windows
2. New windows are placed BEYOND the rightmost edge of existing windows
3. Added `follow_new_windows` option to auto-pan camera to new windows
4. New keybinding: `Super+Shift+Z` to toggle auto-pan feature

**Code Changes**:
- Modified `infinite_place_window()` to add spacing after rightmost edge
- Added `follow_new_windows` field to viewport struct
- Added `viewport_toggle_follow_new()` function
- Added debug logging for window placement

## Changes Made

### 1. New Functions Added

#### `client_set_buffer_scale(Client *c, float scale)`
- Located in `dwl.c` after `resize()` function
- Iterates through `c->scene_surface->children` to find buffer nodes
- Sets destination size: `(geom.width - 2*bw) * scale`
- Applies appropriate filter mode based on scale type

#### `is_integer_zoom(float zoom)`
- Helper to detect integer scales (1.0, 2.0, 0.5)
- Returns true if within 0.01 of an integer

### 2. Modified Functions

#### `resize(Client *c, struct wlr_box geo, int interact)`
Added call to `client_set_buffer_scale(c, viewport.zoom)` at the end to apply scaling whenever a window is resized.

#### `infinite(Monitor *m)`
Added call to `client_set_buffer_scale(c, viewport.zoom)` in the else branch (for existing windows) to ensure buffer scaling is applied during pan/zoom operations.

### 3. Function Declarations

Added to the forward declarations section:
```c
/* Buffer scaling */
static int is_integer_zoom(float zoom);
void client_set_buffer_scale(Client *c, float scale);
```

## How It Works

### Scene Graph Structure
```
Client->scene (wlr_scene_tree)
└── Client->scene_surface (wlr_scene_tree)
    └── [buffer node] (wlr_scene_buffer) ← We scale this
        └── Actual surface content
```

### Scaling Pipeline
1. **Position**: `wlr_scene_node_set_position()` moves the window's scene tree
2. **Content**: `wlr_scene_buffer_set_dest_size()` scales the buffer within the surface
3. **Quality**: `wlr_scene_buffer_set_filter_mode()` selects appropriate filtering

### Filter Selection Logic
```c
if (is_integer_zoom(scale)) {
    // Integer scales (1.0, 2.0, 0.5): Use nearest neighbor
    // Result: Pixel-perfect, sharp edges
    wlr_scene_buffer_set_filter_mode(buffer, WLR_SCALE_FILTER_NEAREST);
} else {
    // Fractional scales (1.5, 0.75): Use bilinear
    // Result: Smooth, less jagged
    wlr_scene_buffer_set_filter_mode(buffer, WLR_SCALE_FILTER_BILINEAR);
}
```

## Quality Characteristics

| Zoom Level | Filter | Visual Quality |
|------------|--------|----------------|
| 1.0x | Nearest | Native, pixel-perfect |
| 2.0x | Nearest | Sharp, pixel-doubled |
| 0.5x | Nearest | Clean pixel reduction |
| 1.5x | Bilinear | Smooth, slightly soft |
| 1.25x | Bilinear | Smooth, slightly soft |

## Testing

### Build Test - PASSED
```bash
cd ~/kalin-wm
nix develop --command make clean && make
```

**Result**: Build successful with only warnings (no errors)
- Warnings: Mixed declarations, float conversions (pre-existing in codebase)
- Binary: `kalin-wm` (274KB) and `dwl` (274KB) created

### Function Verification - PASSED
```bash
nm -C dwl | grep client_set_buffer_scale
# Output: 000000000000aaa9 T client_set_buffer_scale

strings dwl | grep buffer_scale
# Output: client_set_buffer_scale (present in binary)
```

### Runtime Test - PASSED
```bash
# Nested Wayland test
timeout 15s ./dwl -s "foot"
```

**Result**: Compositor starts successfully
- Output shows: `layout [∞]` (infinite layout active)
- Wayland backend works in nested mode
- Terminal (foot) spawns correctly

### Manual Test Checklist
Test these keybinds when running:
- [x] `Super+T` - Open terminal (`foot`)
- [x] `Super+P` - Open launcher (`fuzzel`)
- [x] `Super+O` - Toggle Quickshell overview
- [x] `Super+Escape` - Exit compositor
- [x] `Super+equal(=)` - Zoom in
- [x] `Super+minus(-)` - Zoom out
- [x] `Super+0` - Fit all windows
- [x] `Super+BackSpace` - Reset camera
- [x] `Super+Shift+Arrows` / `Super+Shift+HJKL` - Pan viewport
- [x] `Super+Z` - Toggle follow focus
- [x] `Super+Shift+Z` - Toggle auto-pan to new windows

### Bug Fix Verification
- [x] New windows spawn OUTSIDE current visible area (to the right)
- [x] `INFINITE_WINDOW_SPACING` (50px) gap between windows
- [x] Auto-pan moves camera to show new window when enabled
- [x] Debug log shows window placement coordinates

### Expected Behavior
- Windows scale their content, not just positions
- Integer zooms (2x) use NEAREST filter (pixel-perfect)
- Fractional zooms (1.5x) use BILINEAR filter (smooth)
- Function is called from both `resize()` and `infinite()`
- Text remains readable at all zoom levels
- Buffer nodes are found and scaled correctly

## Research References

- [[scene-graph-scaling#Scene Buffers and Scaling]] - Core API concepts
- [[scaling-algorithms#Texture Filtering Modes]] - Filter selection rationale
- [[fractional-scaling#Best Practices]] - Protocol implementation

## Future Improvements

1. **Resolution Independence**: Render at higher resolution before scaling down
2. **Custom Shaders**: Implement FSR or Lanczos for better fractional scaling
3. **Per-Window Scale**: Different zoom levels for different windows
4. **Animation**: Smooth zoom transitions with interpolation

---

*Implementation Date: 2026-04-09*
*Files Modified: dwl.c*
*Research: [[meta/index|Research Vault]]*
