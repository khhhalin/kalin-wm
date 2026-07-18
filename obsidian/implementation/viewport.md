# Viewport

- The viewport is a camera that looks at the [[infinite-canvas]]. Since
  [[multi-camera]] (2026-07-15) there is one per monitor — `Monitor.cam`, a
  `Viewport` struct — not one global; camera binds act on the monitor under
  the cursor (`selmon`).
- It is implemented as `cam.x`, `cam.y` (in [[world-coordinates]]) and `cam.zoom` (a float, 1.0 = 100%).

- The viewport transforms [[world-coordinates]] into screen coordinates
  (`WORLD_TO_SCREEN_*`/`SCREEN_TO_WORLD_*` in `kalin.h` take a Monitor and
  fold in its layout offset `m->m.x/y`); each window transforms through its
  holder's (`c->mon`) camera.
- Moving a viewport is [[pan]]; scaling it is [[zoom]] (currently parked — see [[zoom]]).

- A viewport can track windows automatically via [[follow-mode]].
- It can frame every window its monitor holds via [[fit-all]], and return to the origin via the reset bind (`Super+BackSpace`).

- Viewports move with a smooth, frame-rate-independent animation.
- The divisor is guarded (`MON_ZOOM_SAFE`) so a camera's zoom can never be zero.

- Viewport changes are reported through the compositor's status output and
  broadcast over the [[ipc-socket]] so the [[quickshell-shell]] can mirror the
  camera state — per-output as a `"viewports"` array, plus a scalar
  `"viewport"` (selmon's camera) for back-compat.
