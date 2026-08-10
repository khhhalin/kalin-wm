# Zoom-scale overhaul

- **Status: planned (root-caused 2026-08-10, not yet started).** A design note
  for reworking the per-frame zoom-scale machinery — the single mechanism
  behind three current, related bugs. Investigate + plan only so far; no code
  has changed (the leftover debug patch in `dwl.c`, below, is still in the tree).
- Intent note (keeper's). As-built pieces it touches: [[buffer-scaling]],
  [[zoom]], [[overview-mode]], [[screenshot-ui]], [[viewport]].

## The three bugs (one root cause)

All three trace to `client_scale_buffers()` (`code/src/dwl.c`, ~3403-3471)
running **every frame for every on-screen client** from `rendermon()`, doing two
coupled, order-dependent things — buffer `dst_size` scaling *and* destructive
in-place rewriting of every subsurface's `child->x/child->y` offset — while a
*third* system (`client_apply_zoom_scale()`, ~dwl.c:3647) changes the client's
actual render DPI on camera settle.

1. **Window internals resize (most visible in screenshots).** The subsurface-offset
   rewrite (~dwl.c:3447-3466) reads the *current* `child->x/y` and multiplies by
   the zoom again — there is **no cached native offset**, so it is only correct if
   always fed native values. When a client re-renders at zoom DPI or commits a new
   subsurface layout, `surface.current.width` and the offsets shift under this code
   and `dst_size` (~dwl.c:3417) no longer matches — internals (subsurface panels,
   popups, CSD) get misplaced/resized. [[screenshot-ui]]'s freeze-frame captures a
   native-DPI shot at a moment when the live scene still has zoom-scaled
   `dst_size`/offsets, so the frozen frame shows the mid-scaled internals.
2. **Zen flickers in [[overview-mode]].** On camera *settle*,
   `client_apply_zoom_scale()` tells clients to re-render at a new DPI
   (`client_set_scale(s, out_scale * zoom)`, ~dwl.c:3661). Zen (heavy,
   Firefox-derived, honors `wp_fractional_scale`) reallocates its whole surface →
   the commit resets `dst_size` → re-triggers `client_scale_buffers` → offset
   re-multiply. That round-trip at the animation boundary (entry via
   `viewport_fit_all()` to ~0.2 zoom, and again on exit) is the flicker. Light
   clients (foot) tolerate it; Zen's expensive reallocation makes it visible.
3. **Camera movement "broke."** The camera code in [[viewport]]
   (`viewport_step_cam`/`viewport_tick`) is intact — no recent commit changed it.
   The regression is the **uncommitted debug patch** in `client_scale_buffers`
   (`KALIN_DEBUG_SCALE` / "SCALE-DBG", ~dwl.c:3438-3469): with the env var set it
   does a `wlr_log(WLR_ERROR)` **per subsurface, per client, per frame** during any
   pan/zoom → a log flood that stalls the render thread → stutter. The patch is
   also mis-indented (a brace-less `if (dbg < 0)` above the `wl_list_for_each`) and
   should be reverted regardless.

## Why it's fragile (the over-engineering)

- **Three overlapping scale systems that must agree every frame:**
  `client_apply_zoom_frame()` (frame/position/border/ring), `client_scale_buffers()`
  (content `dst_size` + subsurface offsets), `client_apply_zoom_scale()` (render DPI
  on settle). Each is re-applied every frame because `wlr_scene` resets `dst_size`
  on commit (~dwl.c:3075).
- **No single source of truth for native geometry.** The subsurface-offset multiply
  is not idempotent — it depends on always seeing native `child->x/y`. There is no
  stored native offset to scale *from*, so any client-driven commit mid-zoom
  corrupts it.
- **Settle-time DPI re-render storms heavy clients.** `client_apply_zoom_scale()`
  induces a reallocation on exactly the clients that reallocate most expensively.

## Design direction (proposed, not signed off)

1. **Step 0 — revert the debug patch** (`KALIN_DEBUG_SCALE` hunk) before anything.
2. **Cache native geometry.** Store each subsurface's native offset once (per
   client/surface) so scaling reads native → screen every time and is idempotent,
   instead of re-multiplying live values.
3. **Apply scale on commit, not per frame.** Drive the content scale from a surface
   commit listener rather than re-deriving it in `rendermon()` every frame — removes
   the per-frame re-apply and the dst_size/offset drift window.
4. **Collapse the three systems into one zoom-application path** with that cached
   native geometry as the single source of truth (frame + content + DPI derived from
   one place).
5. **Gate the settle-time DPI re-render** — skip when target ≈ current, debounce, or
   skip clients that reallocate expensively — so [[overview-mode]] entry/exit doesn't
   storm Zen.

## Key code (for the implementing session)

- `code/src/dwl.c`: `client_scale_buffers()` (3403-3471), `client_set_buffer_scale()`
  (~3481-3500), `client_apply_zoom_scale()` (3647-3665), `rendermon()` per-frame
  scale loop (3079-3092), `viewport_camera_tick()`/`client_apply_zoom_frame()`
  (640-764), the `KALIN_DEBUG_SCALE` patch to revert (~3438-3469).
- `code/src/modules/viewport/viewport_ops.c`: `viewport_step_cam` settle path calling
  `client_apply_zoom_scale()` (~121).
- `code/src/modules/viewport/overview.c`: entry/exit animation boundaries (48-118).
- `code/src/modules/screenshot/screenshot_ui.c`: native capture path (~322-355).
- `code/include/kalin.h`: `WORLD_TO_SCREEN_*`/`MON_ZOOM_SAFE` (~496-500).

## Unblocks / relates

- This is why [[zoom]] is **parked** — the interaction was being rethought; this
  overhaul is the concrete rework of its rendering half.
- Verification must be visual on real hardware (the bugs are per-frame rendering
  artifacts on heavy clients), not just `make test-unit`.
