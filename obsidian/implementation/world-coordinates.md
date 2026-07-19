# World coordinates

- World coordinates are the absolute coordinate system of the [[infinite-canvas]].
- Every window has a world position independent of any [[viewport]] — the
  shared world is single-view ([[multi-camera]]): one global position per
  window, no per-monitor copies, so persistence needed no migration.

- Each monitor's [[viewport]] maps world coordinates to screen coordinates
  using its own position, [[zoom]], and layout offset (`m->m.x/y`).
- Screen coordinates are world coordinates transformed by a specific
  monitor's camera — a window's are computed through its holder's (`c->mon`).

- [[directional-focus]] computes its cone search in world coordinates, so it behaves correctly regardless of how far the user has panned or zoomed.

- Every window stores a fixed world position (`Client.geom.x/y`) and stays there as the camera moves — this applies uniformly now, not just to a special "anchored" subset (see [[connection-graph]]).
