# Directional focus

- Directional focus jumps focus to the nearest window in a given direction.
- It is bound to `Super+Arrows`.

- The algorithm is a cone search in [[world-coordinates]]: a 90° search cone centered on the chosen direction, widened to 180° if no window is found.
- Distance is Euclidean from window center to window center.

- Because the search runs in [[world-coordinates]], it behaves correctly no matter how far the [[viewport]] has panned or zoomed.

- The detailed design lives in [[research/active-design/navigation|the navigation note]].
