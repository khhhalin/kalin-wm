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
- All normal keybinds keep working while it's open (matches niri) — nothing is gated
  behind an "overview mode" state beyond the exit hooks above.

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
