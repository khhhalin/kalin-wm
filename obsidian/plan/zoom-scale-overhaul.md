# Zoom-scale overhaul

- **Status: COMPLETE (root-caused 2026-08-10, landed + verified real hardware 2026-08-24).**
  A design note for reworking the per-frame zoom-scale machinery — the single mechanism
  behind three related bugs plus the later Bug 4. All actionable steps shipped; see
  "Overhaul status: COMPLETE" at the bottom. The narrative below is kept as the original
  root-cause record (line numbers in it are pre-rework and now stale).
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

## Status — targeted fixes landed 2026-08-11 (steps 1, 2)

Two of the five design steps shipped as targeted fixes (commits on `main`,
not pushed), the rest still open:

- **Step 1 — debug patch reverted.** The uncommitted `KALIN_DEBUG_SCALE` hunk was
  discarded — fixes the "camera movement broke" log-flood (bug 3).
- **Step 2 — cache/read native geometry.** `client_scale_buffers()` now scales each
  subsurface offset from the wlroots-native offset (`wlr_subsurface.current.x/y`, via
  new `subsurface_native_offset()`) instead of re-multiplying the mutated node
  position, so the per-frame scaling is idempotent. Falls back to the old value when
  the subsurface can't be resolved (never worse than before). This is the real fix for
  bug 1 (internals resizing) and the most likely cause of the Zen overview flicker
  (bug 2) — offset drift across Zen's subsurfaces during the zoom animation.
- **Bug 2 note:** the settle-time DPI re-render (`client_apply_zoom_scale`) was
  investigated as a flicker cause and ruled out — at overview zoom the target clamps
  back to native, and `client_set_scale()` already no-ops on an unchanged scale, so
  gating it would be dead code. Not done.
- **Verified:** `make clean all` + `make test-unit` (25/25) green. **NOT verified:**
  live rendering on real subsurface clients (Zen) — needs a compositor restart or a
  test-VM visual pass; the unit suite doesn't exercise the render path.
- **Still open (steps 3-5):** apply scale on commit rather than per frame, collapse the
  three scale systems into one path, gate/debounce the settle-time re-render.

## Step A (collapse, = step 4) — DONE + verified real hardware (2026-08-24)

- **Commit 6a1e492.** The three per-frame stages (`client_apply_zoom_frame` frame/border,
  `client_set_buffer_scale` content dst_size + subsurface offsets, `client_apply_crop_clip`
  clip) now route through one dispatcher `client_apply_zoom(c, parts)` with a canonical order
  (frame → scale → clip) and a single `MON_ZOOM_SAFE` read. All four call sites pass the subset
  they historically applied — `rendermon()` `ZOOM_SCALE|ZOOM_CLIP` (reset-on-commit reapply),
  `viewport_camera_tick()` `ZOOM_FRAME` (camera-move), `resize()` `ZOOM_FRAME` then
  `ZOOM_SCALE|ZOOM_CLIP` around `client_set_size()`. The three stage functions are internally
  untouched. **Zero-behavior-change intent.** One normalization: `resize()` now applies
  scale-then-clip (was clip-then-scale); `rendermon()` already used scale-then-clip in
  production, confirming the order is interchangeable.
- **Verified real hardware (independent sonnet assessor).** Deployed via `nixos-rebuild switch`
  → `r8nk1m9b`; note `nixos-rebuild switch` does **not** restart `ly`, so activating the new
  compositor needed `systemctl restart display-manager` (a plain relogin relaunches the old
  session from the still-running DM). Live repro: focus Zen → 1 and 3 overview cycles →
  screenshot; toolbar flush at top, no black gap, no drift, page renders clean — **no Bug-4
  regression, zoom rendering intact.** Builds clean, `make test-unit` green.
## Step B (event-gate, = step 3) — DONE + verified real hardware (2026-08-24)

- **Commit 518d31f.** The unified `client_apply_zoom(SCALE|CLIP)` reapply in `rendermon()` no
  longer runs for every on-screen client every frame. New `Client.zoom_dirty`, set in
  `commitnotify()` (the commit that clobbers `dest_size` runs its wlr_scene reset *after* our
  listener, so the reapply waits for next frame), gates the reapply:
  `if (c->zoom_dirty || (c->mon && c->mon->cam.animating))` then clears the flag. Animating covers
  zoom changing every frame; the settle frame is already a full per-client `resize()` in
  `viewport_step_cam()`; at rest (no commit, static zoom) `dest_size` is untouched so the reapply
  is pure waste and is skipped. Set generously — over-setting only costs a redundant reapply,
  under-setting would leave stale scale.
- **Verified real hardware (independent sonnet assessor).** Deployed `z4v0q27k` (again needed a
  `systemctl restart display-manager` after the switch — see step A note). Live IPC repro: (1)
  zoom-in — content magnifies at 1.15 (scale reapplies under the gate, not stuck at 1×); (2)
  overview out-to-0.3-and-back — Zen returns to a clean, correctly-sized, gap-free 1.0 render, no
  stale scale; (3) no compositor errors during zoom (journal clean but for the pre-existing benign
  "cannot read shaders/*.frag" — the output shader pass is disabled, unrelated). Builds clean,
  `make test-unit` green. **Assessor: SCALED + gap ABSENT, zoom rendering intact.** Note: IPC
  `zoom >1` anchors the camera at a point and walks the window off the framed region (black
  margins) — a camera-framing property, not a scale bug; confirmed by the 1.15 shot rendering
  content magnified and correct.

## Overhaul status: COMPLETE (2026-08-24)

All actionable steps landed and verified on real hardware: step 1 (revert debug patch), step 2
(cache native subsurface geometry), Bug 4 (re-issue clip on commit), step 4/A (collapse three
stages behind `client_apply_zoom`), step 3/B (event-gate the per-frame reapply). Step 5 (gate the
settle-time DPI re-render) stays intentionally undone — dead-code, since `client_set_scale()`
already no-ops on an unchanged scale and overview clamps the target back to native. The per-frame
zoom-scale machinery is now one path, single-source-of-truth on native geometry, applied on
events rather than unconditionally every frame.

## Bug 4 — Zen outline stale after overview settle (reported 2026-08-12, root-cause partial)

- **Symptom (user):** after an overview zoom out/in, a Zen window's border/focus-ring is
  drawn at the wrong geometry and stays wrong *at rest* until the window is manually resized.
  The "outline" is the focus ring (`c->focus_ring[i]`) + border, both sized in
  `client_apply_zoom_frame()` (`dwl.c:657`) from `c->geom * MON_ZOOM_SAFE`.
- **Confirmed invariant violation:** `dwl.c:737` documents that `viewport_tick()` "calls the
  full `arrange()` once when the camera settles, to catch anything a camera-only refresh doesn't
  cover (layout, borders, clip, buffer scale)." It does **not** — `viewport_tick()`
  (`viewport_ops.c:140`) never calls `arrange()`; at settle `viewport_step_cam()`
  (`viewport_ops.c:113-121`) calls only `viewport_camera_tick(m)` + `client_apply_zoom_scale()`.
  Two comments now contradict each other (`dwl.c:737` "arrange at settle" vs `viewport_ops.c:115`
  "Not arrange()") — the path was changed and the documented refresh was dropped.
- **The skip:** `viewport_camera_tick()` re-applies `client_apply_zoom_frame()` but **skips any
  client with `c->animating`** (`dwl.c:755`). A client still spring-gliding when the camera
  settles gets no border re-apply from the settle tick; it relies on the glide-finish
  `resize()` (`client_anim.c:89`) instead.
- **Not fully reproduced by inspection:** in the common orders the *last* writer (glide-finish
  `resize()` or the settle `camera_tick`) still lands the final zoom, so the border ends correct
  — I could not construct the exact persistent-failure sequence statically. The Zen-specific part
  (CSD shadow: border sized from window-geometry `c->geom` while content is sized from the full
  `surface.current`; fractional-scale; async realloc that commits *after* settle) is the piece
  that can't be reasoned about without a live Zen client. **Blocker:** the test-VM runs `foot`,
  not Zen, so it cannot confirm this class. A nested kalin-wm running real Zen, or a hardware
  check, is required to verify any fix. No code changed for this yet (branch `zoom-scale-rework`,
  baseline build + 25/25 green).
- **REPRODUCED + independently confirmed 2026-08-12 (VM, Firefox).** Firefox is Zen's engine and is
  already in the test-VM. Built an automated repro in `test-vm/vm.nix` (`reproDriver`): launches
  Firefox (needs `MOZ_ENABLE_WAYLAND=1` — the VM has no Xwayland), then over the compositor IPC
  socket screenshots a baseline at 1x, zooms out to 0.35 and back (`zoom`/`zoom-reset` = the overview
  cycle), and screenshots again; shots land in `/mnt/host`. Result: **shot A (baseline 1x) has a
  clean amber ring on all four edges; shot C (after the cycle, back at 1x) has the ring's BOTTOM edge
  offset upward — a black gap band and white content overhanging past the ring.** A Sonnet assessor
  confirmed independently: the frame-decoration rect and the surface rect disagree (border too short
  vs content), not merely a moved window. So `c->geom`/frame height is stale/shorter than the actual
  content after settle — a commit-vs-configure / clip-not-reapplied mismatch from the overview scale
  round-trip. **Repeatable pass/fail for any fix: re-run the harness, C must match A.** Fix continues
  on branch `zoom-scale-rework`.
- **FIX (branch `zoom-scale-rework`, commit d36e9b4):** at camera settle,
  `viewport_step_cam()` now forces a full per-client resync (`resize(c, c->geom, 0)` with
  `c->crop.clip_cached = false`) instead of only `viewport_camera_tick()` — so the CSD-shadow clip
  is re-issued at the final zoom even though its computed value is unchanged and wlr_scene reset it
  on the client's post-settle commit. This is the `arrange()`-at-settle the rendermon() comment
  already documents. Builds clean, `make test-unit` 25/25.
- **Verification (VM, sonnet assessor): FIX HOLDS.** Same repro harness, rebuilt with the fix
  (test-vm kalin-wm input temporarily repointed at the worktree, then restored). Post-overview shot C
  now shows the amber ring hugging the content on every captured edge — no black gap, no overhang —
  where the broken build had an obvious detached band. **Caveat:** the one attempt to force Firefox
  fully on-screen so the exact *bottom* edge was captured had Firefox crash (flaky in the 2-core VM),
  so the bottom edge wasn't captured in a clean fixed shot; the fix mechanism is edge-agnostic and no
  misalignment appears anywhere, but final ground-truth is a real-hardware Zen check.
- **Repro harness recipe (for reuse):** in `test-vm/vm.nix`, a `reproDriver` (python over the
  `KALIN_IPC_SOCKET`): launch `firefox` with `MOZ_ENABLE_WAYLAND=1` (no Xwayland in the VM), then
  `screenshot` / `zoom 0.35` / `zoom-reset` / `screenshot`, shots to `KALIN_SHOT_DIR=/mnt/host`. Do
  NOT redirect the driver to the qslog virtio port (single-writer, qs owns it) — inherit the
  compositor stdout. The harness edits were reverted to keep test-vm clean; re-apply from git history
  or this recipe to regression-test.

## Bug 4 — RESOLVED on real hardware (2026-08-20)

- **d36e9b4 was insufficient for real Zen.** Reproduced live on the deployed d36e9b4 build (rmrc3wfq):
  focus Zen → `zoom 0.3` → `zoom-reset` → refocus → `screenshot` over the live IPC socket. Post-cycle
  the Zen toolbar/content sat ~28px lower with a black gap band above it and the frame left at y=0 —
  the exact Bug-4 signature, at rest, on the shipped "fixed" build. The VM Firefox harness could **not**
  reproduce this residual class (both broken and d36e9b4 builds showed a clean top): kalin-wm zoom is
  *buffer*-scale (`dest_size`), so the VM client never sees a scale change and never reallocates, and
  vanilla Firefox's CSD differs from Zen's — the residual gap is Zen-CSD-specific, as this note always
  suspected. **Lesson: this class is only discriminable on real Zen; the VM/Firefox harness cannot see it.**
- **Residual root cause (pinned in code):** a CSD client like Zen reallocates its buffers *after* the
  camera settles (fractional-scale change on the way back from overview); wlr_scene drops the frame
  clip from the rebuilt subsurface tree. `client_apply_crop_clip()` then **skips** re-issuing it — the
  *computed* clip is unchanged (`c->geom` didn't move), so its `clip_cached` guard (there to avoid the
  per-frame re-issue that once spiked commit rate into a GPU hang) matches the stale cache and no-ops.
  The CSD shadow margin shows unclipped → the offset + black gap, stuck until a manual resize moves geom.
- **FIX (commit 6b334a8, merged to main):** invalidate `c->crop.clip_cached = false` in
  `commitnotify()` before the per-commit `resize()`, so `client_apply_crop_clip()` actually re-issues
  the clip onto the freshly-rebuilt subsurface tree. Bounded to once per commit (not a per-frame loop;
  `set_clip` doesn't itself commit) so it cannot reintroduce the GPU-hang the cache guards against.
  Builds clean, `make test-unit` 25/25.
- **Verification (real hardware, independent sonnet assessor): FIX CONFIRMED.** Deployed via
  `nixos-rebuild switch` (home-config kalin-wm path input re-pinned to main@6b334a8 → store
  `82c0ahp0…-kalin-wm`). On the live session: focus Zen → 1 and 3 overview cycles → screenshot; the
  Zen toolbar stays flush at the top with no black band and no content shift, identical to baseline,
  where the d36e9b4 build had the obvious band. Assessor verdict **ABSENT** against the known-broken
  reference. **Bug 4 closed.**
- **Note on nested testing (dead end):** a nested kalin-wm running real Zen *does* reproduce the bug
  (baseline showed the content offset), so it is a valid discriminator — BUT launching a second Zen on
  a machine already running Zen shares process state across instances even with `--no-remote`, and its
  teardown kills the user's live Zen. Nested-with-Zen is hostile to a live session that already runs
  Zen; prefer a live deploy + the reversible live-repro (focus/overview/screenshot, then refocus to
  restore the exact viewport) for this class.
