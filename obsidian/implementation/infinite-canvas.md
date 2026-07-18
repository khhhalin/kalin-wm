# Infinite canvas

- The infinite canvas is an unbounded 2D plane that all windows live on.
- It is the central concept of [[kalin-wm]].

- The canvas replaces workspaces: instead of switching between fixed screens, the user moves a [[viewport]] across one continuous plane.

- Positions on the canvas are given in [[world-coordinates]].
- Every window is free-positioned at a persistent world position (no layout mode, no tiled/floating split); see [[connection-graph]] for how new windows get placed and how windows relate to each other spatially.

- There is one shared canvas but one [[viewport]] (camera) *per monitor* since
  [[multi-camera]]: each monitor looks at its own region of the same plane, and
  a window renders on exactly one monitor at a time (its holder, `c->mon`).
- The portion of the canvas a monitor's camera has panned away from is
  signposted per monitor by [[off-screen-indicators]].
