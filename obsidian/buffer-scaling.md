# Buffer scaling

- Buffer scaling gives each client surface a destination size in the [[scene-graph]] that matches the current [[zoom]], so clients render at the right resolution instead of being stretched.

- It is implemented in `client_set_buffer_scale()`.
- A bug once silently disabled it: the child-node scan compared `node->parent` (a `wlr_scene_tree *`) against a `wlr_scene_node *`, which never matched, so no buffer ever got a destination size.
- The fix compares against the tree itself.
- See the [[ledger]].

- Deep background on scaling lives in [[research/rendering/README|the rendering research]] and [[research/reference/wayland-scaling-glossary|the scaling glossary]].
