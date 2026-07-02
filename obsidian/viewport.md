# Viewport

The viewport is the camera that looks at the [[infinite-canvas]]. It is
implemented as `viewport.x`, `viewport.y` (in [[world-coordinates]]) and
`viewport.zoom` (a float, 1.0 = 100%).

The viewport transforms [[world-coordinates]] into screen coordinates. Moving the
viewport is [[pan]]; scaling it is [[zoom]] (currently parked — see [[zoom]]).

The viewport can track windows automatically via [[follow-mode]]. It can frame
every window at once via [[fit-all]], and return to the origin via the reset bind
(`Super+BackSpace`).

The viewport moves with a smooth, frame-rate-independent animation. Its divisor
is guarded so `viewport.zoom` can never be zero.

Viewport changes are reported through the compositor's status output and
broadcast over the [[ipc-socket]] so the [[quickshell-shell]] can mirror the
camera state.
