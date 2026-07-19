# Wallpaper

The wallpaper is the background rendered behind windows on the
[[infinite-canvas]]. It is drawn by the `ui/wallpaper.c` runtime module as one
shared scene tree of repeating rect-built tiles in `LyrBg`, world-anchored:
sub-tile camera motion moves the tree node, tiles reposition when the camera
crosses a tile boundary.

The intended aesthetic is warm and hand-drawn (a Blue Prince-inspired palette of
yellows, oranges, and browns), matching the look of the [[quickshell-shell]].

## Multi-camera status (Phase 4 analysis, 2026-07-18)

Parallax follows **selmon's camera only** — the monitor under the cursor. The
other monitor shows the same seamless pattern but aligned to the wrong camera,
and the alignment jumps when `selmon` changes. This is a deliberate deferral,
not an oversight:

- True per-monitor parallax **cannot be built from the current rect-tile
  trees at all**, not even with per-monitor tile grids: wlroots scene trees
  don't clip their children, and the sub-tile pan offset means each monitor's
  grid always overhangs its box by up to one tile (640px) — spilling onto the
  neighboring output, where both monitors' grids would fight in `LyrBg`
  (whichever is later in the child order wins, showing a wrongly-aligned seam
  that scrolls with the *other* monitor's camera).
- The workable design is **wallpaper as a texture**: render the tile pattern
  once into a `wlr_buffer` (the renderer `drw` and allocator `alloc` are
  already externed in `kalin.h`), then give each monitor one
  `wlr_scene_buffer` sized exactly `m->m` and pan it via
  `wlr_scene_buffer_set_source_box` (buffer ≥ monitor + one tile; wrap the
  source offset modulo tile size). A scene buffer clips to its destination
  size, so nothing spills. Per-monitor state can live in module statics with
  the same lazy-create/GC-against-`mons` pattern [[off-screen-indicators]]
  uses; the `Wallpaper` struct in `kalin.h` and its `wallpaper` global in
  dwl.c would become dead and should be removed in the same change
  (keeper-level: `kalin.h`/dwl.c edits). Rendering into a buffer is
  GPU-path work that needs real-output verification, so it wasn't attempted
  blind from a worker lane.

See [[multi-camera]] for the phase context.
