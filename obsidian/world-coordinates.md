# World coordinates

World coordinates are the absolute coordinate system of the [[infinite-canvas]].
Every window has a world position independent of the [[viewport]].

The [[viewport]] maps world coordinates to screen coordinates using its position
and [[zoom]]. Screen coordinates are world coordinates transformed by the camera.

[[directional-focus]] computes its cone search in world coordinates, so it
behaves correctly regardless of how far the user has panned or zoomed.

An [[anchored-window]] stores a fixed world position and stays there as the
camera moves.
