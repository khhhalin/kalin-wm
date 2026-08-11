# Crop as True Clipping (Not Resize)

## Status

- 2026-04-13: **Phase 1/2 started** in active code path (`resize()`):
   - crop now uses scene-surface offset + clip rectangle
   - client configure size is kept at base size while cropped
   - further validation/tuning still required (xdg offsets, edge cases)
- 2026-04-13: **Phase 3 in progress**:
   - crop apply now derives visible footprint from base size explicitly
   - clip-bound clamping hardened to avoid invalid regions with geometry offsets

## Problem Statement

Current crop behavior changes window size instead of only masking visible content.

Observed in active pipeline code:

- Crop selection stores normalized crop rectangle (`crop.x/y/w/h`) but then resizes geometry in [src/modules/crop_mode.c](src/modules/crop_mode.c#L234-L241).
- `resize()` always sends `client_set_size()` using current geometry in [dwl.c](dwl.c#L2806-L2808).
- Clip is currently based on full geometry/default surface clip in [client.h](client.h#L142-L160) and [dwl.c](dwl.c#L2809-L2810), without using `crop.x/y/w/h`.

Result: crop acts like destructive resize, not viewport-style clipping.

## Desired Behavior

When cropping:

1. Client keeps its real/base size (no client-side reconfigure caused by crop).
2. Compositor shows only selected portion of content.
3. Layout footprint may shrink to cropped visible size (optional policy), but client buffer dimensions remain base size.
4. Uncrop restores full visibility instantly without content reflow artifacts.

## Design Direction

Implement crop as **scene-node offset + clip rectangle**, not geometry resize of the client buffer.

### Core idea

- Treat `crop.base_w/base_h` as canonical full size.
- Treat `geom.width/height` during crop as visual frame/footprint only.
- Compute crop source rectangle in base-content pixels:
  - $src_x = crop.x \cdot base\_inner\_w$
  - $src_y = crop.y \cdot base\_inner\_h$
  - $vis_w = crop.w \cdot base\_inner\_w$
  - $vis_h = crop.h \cdot base\_inner\_h$
- Shift `scene_surface` by negative source offset.
- Apply clip rectangle to visible region.
- Keep `client_set_size()` at base size while `crop.active`.

## Implementation Plan

### Phase 1: Decouple visual size from configure size

1. In [dwl.c](dwl.c#L2742-L2813), change `resize()` behavior:
   - If `crop.active`, do **not** send cropped dimensions to client.
   - Send base (uncropped) size through `client_set_size()`.
   - Keep frame geometry for layout/scene placement.
2. Ensure `base_w/base_h` are always valid before entering crop path.

### Phase 2: Apply true crop transform

1. Introduce helper in compositor core (or pipeline crop module):
   - `crop_compute_pixels(Client *c, int *src_x, int *src_y, int *vis_w, int *vis_h)`.
2. In `resize()` scene update block:
   - Set `scene_surface` position to `bw - src_x`, `bw - src_y` when cropped.
   - Use clip rect equal to visible crop bounds (with XDG geometry offsets where needed).
3. Keep border/frame size at visible footprint (`vis_w/vis_h` + border).

### Phase 3: Crop workflow updates

1. In [src/modules/crop_mode.c](src/modules/crop_mode.c#L234-L241), replace "resize to crop size" with:
   - update crop state
   - update visual footprint
   - arrange neighbors by visual delta (existing column adjustment logic can remain with new delta source)
2. Keep uncrop behavior by disabling crop state and restoring full visible footprint from base.

### Phase 4: Edge cases and correctness

1. XDG geometry offsets in clip calculations.
2. Border width interactions (avoid off-by-one / border bleed).
3. Zoom + crop interaction (crop in world/content space, then zoom).
4. Fullscreen/floating behavior policy consistency.

## Acceptance Criteria

1. Cropping no longer triggers client-side resize semantics.
2. Terminal/editor internal layout does not reflow solely due to crop.
3. Only selected content area is visible; outside region is fully masked.
4. Uncrop restores full content without size drift.
5. Works with pan/zoom/follow camera.
6. Works across repeated crop/uncrop cycles.

## Manual Test Matrix

- Crop center region, verify hidden margins.
- Crop top-left, verify correct source alignment.
- Re-crop same window multiple times.
- Crop + viewport pan/zoom.
- Crop in column flow and verify neighbor stacking stability.
- Uncrop, then close/reopen (with size persistence) and verify base-size behavior remains correct.

## Risks

- Clip coordinate mismatch between scene graph node-space and XDG geometry-space.
- Regressions in subsurface clipping for complex clients.
- Border/clip misalignment under non-default `bw`.

## Notes

The file [src/modules/crop_viewport_layout.c](src/modules/crop_viewport_layout.c) is deprecated; active behavior is in `src/modules/*` and included via [dwl.c](dwl.c#L2688-L2693).
