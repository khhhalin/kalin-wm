# multi-camera

- **Status: in progress (started 2026-07-15).** Per-monitor independent cameras over the single shared [[infinite-canvas]], replacing the one global [[viewport]].
- Decided with Kalin (2026-07-15): **single-view shared world** — one world, every window keeps one global position, each monitor has its own camera (pan + zoom), and a window renders on exactly one monitor at a time (`c->mon` is the holder). Full multi-view (same window live on two overlapping cameras) was considered and deferred — it needs per-output scene-node duplication for every client (borders/focus-ring/anim/crop all per-output); single-view keeps all existing per-client machinery with a camera parameter.
- UX contract: **the monitor under the cursor owns all camera input** (pan/zoom/fit/follow/overview act on `selmon`, which already follows the cursor) — park one monitor on a region, roam the other.

## Model

- `Monitor` gains a full `Viewport cam` (x/y/zoom + targets/anim/coast state). The global `viewport` instance is removed.
- Transform becomes monitor-relative, folding in the monitor's layout offset (today the camera is anchored at layout (0,0) and monitors are adjacent slices of one view):
  `screen_x = (world_x - m->cam.x) * m->cam.zoom + m->m.x`
  For a single monitor at (0,0) this is byte-identical to today's behavior.
- Every client transforms through **its holder's** camera (`c->mon`). Docked/fullscreen/maximized clients keep their existing camera bypass.
- Cross-monitor moves (both decided 2026-07-15): drag hand-off (Super+drag crossing the physical boundary reassigns `c->mon` and re-bases the window under the cursor through the new camera) + an explicit send-to-monitor bind (teleport to the other camera's center). Cross-monitor [[connection-graph]] edges are severed on hand-off (lines through two different cameras are meaningless).
- Follow-up ideas (not v1): camera "claiming" (a window pops to the other monitor when only that camera covers it); per-output clipping of viewport-edge spill; full multi-view.

## Touch list (from the 2026-07-15 exploration; file:line as of then)

- `kalin.h` — `Viewport` per Monitor; `WORLD_TO_SCREEN_*`/`SCREEN_TO_WORLD_*` macros take a Monitor.
- `dwl.c` — `client_apply_zoom_frame` (c->mon cam + m->m offset), `viewport_camera_tick`, `rendermon` buffer-scale (`client_set_buffer_scale(c, c->mon->cam.zoom)`), spawn placement (`SCREEN_TO_WORLD` vs selmon), interactive move/resize, floating clamp (per m->m, not sgeom), spotlight, printstatus.
- `modules/viewport/viewport_ops.c` — every op takes the target Monitor (selmon at bind call sites); `viewport_tick` iterates all mons' cameras.
- `modules/viewport/overview.c` — per-selmon save/restore.
- `modules/input/gestures.c` — write selmon's cam.
- `modules/crop/crop_mode.c`, `modules/layout/connection_graph.c`, `modules/layout/client_anim.c`, `modules/layout/directional_focus.c` — transform via the client's/selmon's camera.
- `modules/ui/wallpaper.c` — per-monitor wallpaper offset (parallax follows each monitor's own camera).
- `modules/ui/offscreen_indicators.c` — per-monitor (indicators for that monitor's windows against its camera, within m->m, not sgeom).
- `modules/ipc.c` — broadcast becomes per-output: `"viewports":[{"output":name,x,y,zoom,follow,follow_new}...]`; the pre-transformed rects (focused/connections/pending) stay single-valued thanks to single-view but are computed via each client's holder camera.
- Shell (`~/environment/quickshell`): `KalinViewport.qml` parses the per-output list into a map keyed by output name; `ConnectionLines.qml`/`Overview.qml`/`WindowPeek.qml`/`Osd.qml` pick their own screen's entry.
- Persistence: window world coords unchanged (shared world) — no migration; optionally save per-monitor cameras later.
- Docking/[[bar-tuis]] panels: unaffected (already camera-bypassed screen-space rects).

## Phases

1. **Core — DONE (2026-07-15).** `Monitor.cam` replaces the global `viewport`; `WORLD_TO_SCREEN_*`/`SCREEN_TO_WORLD_*` macros take a monitor (param renamed `mon_` — a param named `m` gets substituted into the `->m` box member access and miscompiles). Every transform site routes through the client's holder (`c->mon`) or the cursor monitor (`selmon`): `viewport_ops.c` (all ops + `viewport_tick` iterates every animating camera), `overview.c` (per-monitor save/restore), `gestures.c`, `crop_mode.c`, `connection_graph.c`, `client_anim.c`, `directional_focus.c`, `dwl.c` (zoom-frame, spawn, move/resize, clamp per `m->m` not `sgeom`, spotlight, buffer scale, `printstatus` per-output), `ipc.c` (rects per-holder; new `"viewports":[{output,x,y,zoom,follow,follow_new}]` array + scalar `"viewport"` = selmon's cam for back-compat). Verified: build + 25 unit tests green; single-monitor nested = no regression; **nested dual-output (`WLR_WL_OUTPUTS=2`) shows two independent cameras panning independently.** Wallpaper still follows selmon only (one shared tree — phase 4).
2. **Hand-off** — drag reassign + send-to-monitor bind + connection severing. *Not started.*
3. **IPC/shell** — shell keys the `viewports` array by output; overlays (ConnectionLines/Overview/WindowPeek) pick their own screen's entry. *Not started — the scalar `viewport` keeps the shell working meanwhile; per-screen connection-line offset was already imperfect pre-multicam.*
4. Per-monitor wallpaper, off-screen indicators, overview polish; vault updates ([[viewport]], [[infinite-canvas]], [[world-coordinates]], [[keybindings]]).
