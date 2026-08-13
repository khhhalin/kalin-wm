# Overview mode

- Overview mode is a native, compositor-rendered zoom-out of the [[infinite-canvas]] —
  modeled on niri's Overview (`Mod+O`/`toggle-overview`, shipped niri v25.05): zoom the
  real camera out to see everything spatially, click a window to jump to it.
- Triggered by `Super+O` (`code/src/modules/viewport/overview.c`). Not shell-delegated
  any more — kalin-wm's own scene *is* a real camera over a real 2D canvas, so "zoom
  out" already is the overview; no separate renderer or thumbnail grid needed.

## Behavior

- `Super+O`: saves the current [[viewport]] camera position/zoom, then reuses
  [[fit-all]]'s exact bounding-box+zoom shot (`viewport_fit_all()`) to frame every
  window at once.
- Click a window while open: focuses it (already worked at any zoom level — hit-testing
  is zoom-aware) and pans/zooms the camera to center on *that* window at 1.0 zoom —
  jumping to what you clicked, computed directly for the target zoom (not via
  `viewport_center_on()`, which centers for the *current*, still-zoomed-out, live zoom
  and would land off target once the camera finished zooming in).
- `Super+O` again, or a bare `Escape` (without clicking a window), restores the camera
  to exactly where it was before `Super+O` — a plain dismiss, distinct from clicking.
  Bare `Escape` is a no-op when the overview isn't open (mirrors the same
  active-mode-only bare-key pattern [[crop-mode]]'s `r`-to-reset uses).
- Per-monitor ([[multi-camera]], Phase 4 polish 2026-07-18): the overview
  hijacks and restores exactly *one* monitor's camera (the one under the
  cursor when it opened — a parked second monitor stays parked). `Super+O`
  while the overview is open on a *different* monitor moves it: the old
  monitor's camera is restored and the overview opens on the monitor under
  the cursor (which owns all camera input). Clicking a window held by a
  *different* monitor dismisses with restore instead of jumping — that window
  is already showing through its own un-hijacked camera (previously this
  dropped the overview state without restoring, leaving the hijacked camera
  stuck at the fit-all shot). If the overview monitor is destroyed while
  open, the saved state is discarded safely (`overview_mon` is re-validated
  against `mons` before any restore — dwl.c frees monitors with no hook into
  this module).
- All normal keybinds keep working while it's open (matches niri) — nothing is gated
  behind an "overview mode" state beyond the exit hooks above. This includes
  `Super+BTN_LEFT`/`Super+BTN_RIGHT` (move/resize): `buttonpress()` (`dwl.c`) skips
  the click-to-jump path while Super is held, specifically so a Super-held click
  starts a move/resize grab instead of immediately jumping the camera to the clicked
  window and closing the overview — the whole point of opening it is to rearrange
  windows while seeing all of them at once, which a jump-on-click would have made
  impossible. A *plain* click (no Super) still jumps, as before.
- The `"overview"` IPC field was originally added so the shell's connection-graph
  lines (`ConnectionLines.qml`, [[quickshell-shell]]) could show whenever overview
  is open, not just while Super is held. The [[connection-graph]] and its
  `connections` broadcast were **removed 2026-08-13** ([[layout-impl]]); the field
  itself stays (still useful state), and the shell overlay that consumed it
  (`ConnectionLines.qml`) was deleted the same day (kalin-shell `5788f49`).

## Known issue — Zen flickers on entry/exit (root-caused 2026-08-10)

- Opening the overview (`viewport_fit_all()` → ~0.2 zoom) and closing it make
  Zen (heavy, Firefox-derived) flicker. Not an overview-logic bug: on camera
  *settle* the per-frame zoom-scale machinery re-renders the client at a new DPI
  (`client_apply_zoom_scale()`), Zen reallocates its whole surface, and the
  resulting commit storm at the animation boundary is the flicker. Light clients
  (foot) don't show it. Shared root cause with the screenshot/internals-resize
  bug — full analysis and the planned fix in [[zoom-scale-overhaul]] / [[buffer-scaling]].

## Not in this pass (follow-up if wanted)

- Right-drag-to-pan / scroll-to-pan without holding a modifier while open (niri's
  "pointing devices get easier" convenience) — the existing pan/zoom binds and normal
  click/drag already work at any zoom level; this is a convenience, not core behavior.
- A hot-corner or touchpad-gesture trigger.
- Quickshell's older `Overview.qml`/`OverviewState.qml` (a shell-rendered grid of
  `ScreencopyView` thumbnails) still exists and is still used by niri's own
  unrelated native overview and `CompositorService`'s niri fallback path — kalin-wm's
  *trigger* is native (above), the shell grid wasn't the thing that changed for that.
  Its thumbnails share the same underlying per-window capture path as the taskbar
  hover-preview (`WindowPeek.qml`), so they benefited the same way once
  `hyprland-toplevel-export-v1` was implemented — see [[protocols]] and the [[ledger]].
  Both `Overview.qml` and `WindowPeek.qml` *were* touched later (2026-07-09, see
  [[stability]] and the [[ledger]]) to throttle `ScreencopyView` capture from
  continuous `live: true` to a periodic `captureFrame()` — chronic dmabuf-negotiation
  failures under continuous live capture were a real driver of the quickshell bar's
  recurring crashes.
