# Anchored window

An anchored window is a window detached from the [[column-layout]] strip that
stays at a fixed [[world-coordinates|world position]] on the [[infinite-canvas]].

An anchored window keeps its place as the camera pans and zooms; it is not
repositioned by the column layout. Its position can be moved by keyboard.

Anchoring is one of the two ways a window lives on the canvas — the other is
being placed in the [[column-layout]]. [[window-placement]] decides which a new
window becomes.

Anchored window positions are saved and restored by [[persistence]].
