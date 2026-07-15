# Scene graph

- The scene graph is [[wlroots]]'s tree of render nodes (`wlr_scene`).
- kalin-wm positions windows in the scene graph according to the [[viewport]] transform of their [[world-coordinates]].

- [[zoom]] is applied as a true scale on the scene graph, so windows render at the zoomed resolution rather than being magnified bitmaps.
- [[buffer-scaling]] sets each surface's destination size within the scene graph.

- Scene-node creation is checked for failure during window mapping, a guard added during the [[stability]] audit.
