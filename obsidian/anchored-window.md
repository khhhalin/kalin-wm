# Anchored window

**SUPERSEDED — meaningless now.** "Anchored" used to distinguish a window
detached from the [[column-layout]] from one placed in it. Since every
window is now free-positioned at all times (see [[connection-graph]]), that
distinction no longer exists — there's nothing to be anchored *relative to*.

The rest of this note describes the old design, kept for history:

- An anchored window was a window detached from the [[column-layout]] strip that stayed at a fixed [[world-coordinates|world position]] on the [[infinite-canvas]].
- It kept its place as the camera panned and zoomed, and was not repositioned by the column layout.
- Anchoring was one of the two ways a window lived on the canvas — the other was being placed in the [[column-layout]]. [[window-placement]] decided which a new window became.
