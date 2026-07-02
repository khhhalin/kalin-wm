# Performance

> Optimization techniques and performance patterns.

## Areas

| Topic | Document | Description |
|-------|----------|-------------|
| [Buffer Management](../rendering/buffer-management.md) | DMA-BUF | Avoid CPU copies |
| [Scene Graph Scaling](../reference/wlroots/scene-graph-scaling.md) | Region updates | Scene graph and damage behavior |
| [Protocol Matrix](../protocols/protocol-matrix.md) | Capability status | Rendering/protocol readiness |

---

## Quick Tips

### Buffer Management

```c
// Prefer DMA-BUF for client buffers
wlr_linux_dmabuf_v1_create_with_renderer(dpy, renderer);

// Use direct scanout when possible
wlr_output_attach_buffer(output, buffer);
```

### Rendering

```c
// Enable damage tracking
wlr_output_damage *damage = wlr_output_damage_create(output);

// Only render damaged regions
pixman_region32_t *damage_region = wlr_output_damage_get(damage);
```

### Scene Graph

```c
// Use scene graph for automatic damage
struct wlr_scene *scene = wlr_scene_create(dpy);

// Reuse scene buffers
wlr_scene_buffer_set_buffer(scene_buffer, buffer);
```

---

## Performance Checklist

- [ ] DMA-BUF enabled for zero-copy
- [ ] Direct scanout working
- [ ] Damage tracking enabled
- [ ] Scene graph buffer reuse
- [ ] Fractional scale protocol active
- [ ] VSync synchronized

---

*See [Buffer Management](../rendering/buffer-management.md) for detailed buffer optimization.*
