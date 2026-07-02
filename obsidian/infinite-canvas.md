# Infinite canvas

The infinite canvas is an unbounded 2D plane that all windows live on. It is the
central concept of [[kalin-wm]].

The canvas replaces workspaces: instead of switching between fixed screens, the
user moves a [[viewport]] across one continuous plane.

Positions on the canvas are given in [[world-coordinates]]. A window is placed on
the canvas either by the [[column-layout]] or as an [[anchored-window]].

The [[viewport]] is the camera that looks at the canvas. The portion of the
canvas outside the viewport is signposted by [[off-screen-indicators]].
