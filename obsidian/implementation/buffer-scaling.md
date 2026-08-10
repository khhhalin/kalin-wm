# Buffer scaling

- Buffer scaling gives each client surface a destination size in the [[scene-graph]] that matches the current [[zoom]], so clients render at the right resolution instead of being stretched.

- It is implemented in `client_set_buffer_scale()`.
- A bug once silently disabled it: the child-node scan compared `node->parent` (a `wlr_scene_tree *`) against a `wlr_scene_node *`, which never matched, so no buffer ever got a destination size.
- The fix compares against the tree itself.
- See the [[ledger]].

- Deep background on scaling lives in [[research/rendering/README|the rendering research]] and [[research/reference/wayland-scaling-glossary|the scaling glossary]].

## Current reality — a fragile three-system tangle (root-caused 2026-08-10)

- What the note above describes (`dst_size` matching zoom) is only *half* of what
  `client_scale_buffers()` (`code/src/dwl.c` ~3403-3471) actually does. It also
  **rewrites every subsurface's `child->x/child->y` offset in place**, multiplying by
  the zoom — with **no cached native offset**. That multiply is only correct if it is
  always fed native values; a client re-render at zoom DPI or a subsurface re-layout
  shifts `surface.current.width` and the offsets under it, and the offset scaling then
  no longer matches `dst_size`.
- It runs **every frame for every on-screen client** (from `rendermon()`), because
  `wlr_scene` resets `dst_size` on commit.
- Two other systems must agree with it each frame: `client_apply_zoom_frame()`
  (frame/border/ring position) and `client_apply_zoom_scale()` (~dwl.c:3647, the
  render-DPI re-render on camera settle).
- **This tangle is the single root cause of three current bugs** — window internals
  resizing (visible in [[screenshot-ui]]), Zen flickering in [[overview-mode]], and
  "camera movement broke" (the last is actually a leftover `KALIN_DEBUG_SCALE` debug
  patch flooding the log per-subsurface-per-frame). Full root-cause + the planned
  rework: **[[zoom-scale-overhaul]]**.

