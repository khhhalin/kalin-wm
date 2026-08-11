# multicam-phase4 — worker report

Branch: `fleet/multicam-phase4`. Status: **for-review**.
Code commit: `c245be9`; vault/task-note commits follow it on the branch.

## What changed and why

### Off-screen indicators — shipped (`code/src/modules/ui/offscreen_indicators.c`, rewritten)

The old module was one global set of four markers positioned against `sgeom`
and fed by *every* client — under per-monitor cameras that lit up all screens
whenever any one camera lost a window. Now each monitor gets its own marker
set: only that monitor's held windows (`VISIBLEON(c, m)`, i.e. `c->mon == m`)
are tested, against and clamped to `m->m`.

Implementation notes:
- The module cannot hook monitor create/destroy (dwl.c owns the lifecycle and
  is out of scope), so sets are created lazily in the per-frame update and
  GC'd by membership check against `mons` — covering both destroyed monitors
  (`cleanupmon()` frees the struct; the stale pointer is only compared, never
  dereferenced) and disabled ones (`closemon()` zeroes `m->m`; stale markers
  at old layout coords could otherwise land on whichever live monitor takes
  that region over).
- The off-screen test now scales the window's world-space size by the holder
  camera's zoom (`c->geom` is world-sized; scene coords are screen-space) —
  the old code compared world width against screen edges, over-reporting at
  zoom < 1.
- Camera-bypassed clients (`isfullscreen || ismaximized || docked`, same
  predicate as `client_apply_zoom_frame()`) are skipped like panels — glued
  to the screen by construction.
- `offscreen_indicators_configure()` is kept as a documented no-op: dwl.c
  still calls it (from `updatemons()`, *before* per-monitor boxes are
  refreshed), and positions now derive from each `m->m` lazily.

### Overview — three per-camera fixes (`code/src/modules/viewport/overview.c`)

1. **Dangling `overview_mon`**: destroying the overview's monitor while open
   left `overview_mon` pointing at freed memory; the next exit wrote camera
   targets through it. `overview_mon_alive()` re-validates membership in
   `mons` before any restore; if the monitor is gone the saved state is
   discarded safely.
2. **`Super+O` on a different monitor** used to invisibly restore the *other*
   screen's camera and do nothing under the cursor. It now moves the
   overview: restore the old monitor, open on `selmon` — the monitor under
   the cursor owns all camera input (the multi-camera UX contract).
3. **Clicking a window held by a different monitor** used to drop the
   overview state without restoring the hijacked camera (stuck at the
   fit-all shot, saved position lost) and then jump the *other* monitor's
   un-hijacked camera — doubly wrong. It now dismisses with restore; the
   focus click still lands normally in `buttonpress()`.

Checked and found already correct for the per-camera model: save/restore of
animation *targets* (not mid-flight values), `viewport_fit_all()` fitting
only `selmon`'s held windows against `m->w`, and `overview_select()`'s
centering math (matches `viewport_center_on()`'s idiom at zoom 1).

### Wallpaper — honest partial: analysis, not code (`code/src/modules/ui/wallpaper.c`, comment only)

Parallax still follows `selmon` only. True per-monitor parallax **cannot be
built inside wallpaper.c with the current rect-tile design**, and not because
of the shared tree alone: even per-monitor tile grids fail, because wlroots
scene trees don't clip children and the sub-tile pan offset makes each grid
overhang its monitor's box by up to one tile (640px), spilling onto the
neighboring output where the two grids would fight in `LyrBg` (a
wrongly-aligned seam scrolling with the other monitor's camera).

**Recommended seam (keeper-level):** wallpaper as a texture. Render the tile
pattern once into a `wlr_buffer` (renderer `drw` + allocator `alloc` are
already externed in `kalin.h`), then one `wlr_scene_buffer` per monitor sized
exactly `m->m`, panned via `wlr_scene_buffer_set_source_box` (buffer ≥
monitor + one tile; wrap source offset modulo tile size). Scene buffers clip
to their destination size — no spill, exact per-monitor parallax, sub-pixel
pan. Per-monitor state can use the same lazy-create/GC pattern as the new
indicators. Exact edits needed outside my scope:
- `code/include/kalin.h` (~line 504–514): the `Wallpaper` struct becomes dead
  (state moves into wallpaper.c statics or per-monitor entries) — remove it
  and its `extern Wallpaper wallpaper` (line 649).
- `code/src/dwl.c` (~line 201): remove the `Wallpaper wallpaper;` definition;
  the `wallpaper_configure(sgeom.width, sgeom.height)` call in `updatemons()`
  (~line 4635) can stay or become per-monitor.
- GPU-path work (buffer render pass, format negotiation) needs real-output
  verification — deliberately not attempted blind from a worker lane.

Full analysis recorded in `obsidian/implementation/wallpaper.md`.

## Files touched

- `code/src/modules/ui/offscreen_indicators.c` — rewritten per-monitor.
- `code/src/modules/viewport/overview.c` — three fixes above.
- `code/src/modules/ui/wallpaper.c` — comment updated to point at the analysis.
- Vault: `implementation/multi-camera.md` (Phase 4 marked done-except-wallpaper,
  as-built detail), `off-screen-indicators.md`, `overview-mode.md`,
  `wallpaper.md` (rewritten with the seam analysis), and the doc sweep of
  `viewport.md`, `infinite-canvas.md`, `world-coordinates.md` (per-monitor
  camera language). `tasks/multicam-phase4.md` created (it was absent at
  dispatch despite the brief referencing it) with status: for-review.

## Verification

- Baseline before changes: `nix develop -c make clean all` exit 0;
  `nix develop -c make test-unit` all pass.
- After changes: `nix develop -c make clean all` exit 0 (no new warnings);
  `nix develop -c make test-unit` → "OK: all checks passed".
- No unit tests were added: the changed behavior is scene-graph/monitor
  lifecycle code with no existing test seam (`test-unit` covers pure helpers;
  indicators/overview need a running compositor).
- **Not run (keeper-only per brief): VM tests, dual-output/GPU verification.**
  The indicator GC, overview move-between-monitors, and disabled-output paths
  specifically want a `WLR_WL_OUTPUTS=2` nested run at the gate.
