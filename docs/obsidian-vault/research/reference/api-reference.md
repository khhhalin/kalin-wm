# API Quick Reference

> Quick lookup for key APIs mentioned in the research vault.

---

## wlroots Scene API

### Buffer Scaling

| Function | Purpose | See Details |
|----------|---------|-------------|
| `wlr_scene_buffer_set_dest_size()` | Set output size (scaling) | [[scene-graph-scaling#Scaling Buffers]] |
| `wlr_scene_buffer_set_source_box()` | Crop source buffer | [[scene-graph-scaling#Cropping Buffers]] |
| `wlr_scene_buffer_set_filter_mode()` | Quality (bilinear/nearest) | [[scene-graph-scaling#Scaling Quality]] |
| `wlr_scene_node_set_position()` | Position in scene | [[scene-graph-scaling#Positioning Nodes]] |

```c
// Example: Scale buffer to 800x600 with bilinear filtering
wlr_scene_buffer_set_dest_size(buffer, 800, 600);
wlr_scene_buffer_set_filter_mode(buffer, WLR_SCALE_FILTER_BILINEAR);
```

### Scene Trees

| Function | Purpose | See Details |
|----------|---------|-------------|
| `wlr_scene_create()` | Create root scene | [[scene-graph-scaling#Creating a Scene]] |
| `wlr_scene_tree_create()` | Create sub-tree | [[scene-graph-scaling#Creating Scene Trees]] |
| `wlr_scene_node_reparent()` | Move nodes between trees | [[scene-graph-scaling#Nested Scene Trees]] |

```c
// Create zoomable workspace tree
struct wlr_scene_tree *workspace = wlr_scene_tree_create(&scene->tree);
// ... add windows to workspace
wlr_scene_node_set_position(&workspace->node, pan_x, pan_y);
```

---

## wlroots Fractional Scale

| Function | Purpose | See Details |
|----------|---------|-------------|
| `wlr_fractional_scale_manager_v1_create()` | Create global | [[fractional-scaling#wlroots API]] |
| `wlr_fractional_scale_v1_notify_scale()` | Notify surface | [[fractional-scaling#Implementation Details]] |

```c
// Setup (once at startup)
struct wlr_fractional_scale_manager_v1 *mgr =
    wlr_fractional_scale_manager_v1_create(display, 1);

// Notify surface (when scale changes)
wlr_fractional_scale_v1_notify_scale(surface, 1.5);  // 150%
```

### v120 Encoding

| Scale | v120 Value |
|-------|------------|
| 1.0 | 120 |
| 1.25 | 150 |
| 1.5 | 180 |
| 2.0 | 240 |

```c
uint32_t v120 = round(scale * 120);
```

**See**: [[fractional-scaling#Protocol Basics]]

---

## Buffer Management

| Function | Purpose | See Details |
|----------|---------|-------------|
| `wlr_buffer_lock()` | Reference buffer | [[buffer-management#wlr_buffer and wlr_texture]] |
| `wlr_buffer_unlock()` | Release reference | [[buffer-management#Buffer Lifecycle]] |
| `wlr_texture_from_buffer()` | Import to GPU | [[buffer-management#Texture Import]] |

```c
// Zero-copy buffer → texture
struct wlr_texture *tex = wlr_texture_from_buffer(renderer, buffer);
```

### DMA-BUF

| Function | Purpose | See Details |
|----------|---------|-------------|
| `wlr_dmabuf_attributes` | Buffer attributes | [[buffer-management#DMA-BUF Attributes]] |
| `wlr_texture_from_dmabuf()` | Import DMA-BUF | [[buffer-management#DMA-BUF Import]] |

**See**: [[buffer-management#Linux DMA-BUF and Zero-Copy Buffer Sharing]]

---

## Scale Filters

| Filter | Enum | Best For | Performance |
|--------|------|----------|-------------|
| Nearest | `WLR_SCALE_FILTER_NEAREST` | Integer scales, pixel art | Fastest |
| Bilinear | `WLR_SCALE_FILTER_BILINEAR` | Fractional scales, smooth | Fast |

```c
// Set at buffer creation or when scale changes
wlr_scene_buffer_set_filter_mode(buffer, WLR_SCALE_FILTER_BILINEAR);
```

**See**: [[scaling-algorithms#Texture Filtering Modes]], [[scene-graph-scaling#Scaling Quality and Filter Modes]]

---

## Protocols

### wp_fractional_scale_v1

```c
// Client receives preferred scale
static void preferred_scale_handler(void *data, 
    struct wp_fractional_scale_v1 *fractional_scale, 
    uint32_t scale) {
    double fractional_scale = scale / 120.0;  // v120 decoding
}
```

**See**: [[fractional-scaling#wp_fractional_scale_v1 Protocol]]

### wp_viewporter

```c
// Client sets source crop and destination size
wp_viewport_set_source(viewport, src_x, src_y, src_width, src_height);
wp_viewport_set_destination(viewport, dst_width, dst_height);
```

**See**: [[buffer-management#Buffer Cropping and Scaling]]

### wp_linux_dmabuf

```c
// Client creates DMA-BUF
struct zwp_linux_buffer_params_v1 *params = 
    zwp_linux_dmabuf_v1_create_params(dmabuf);
zwp_linux_buffer_params_v1_add(params, fd, plane_idx, offset, stride, modifier);
struct wl_buffer *buffer = zwp_linux_buffer_params_v1_create_immed(params, 
    width, height, format, flags);
```

**See**: [[buffer-management#wp_linux_dmabuf Protocol]]

---

## Coordinate Transforms

### World → Screen

```c
// With zoom and pan
int screen_x = (world_x - camera_x) * zoom;
int screen_y = (world_y - camera_y) * zoom;
```

**See**: [[scene-graph-scaling#Coordinate Transformations]]

### Buffer → Surface → Output

| Transform | Formula |
|-----------|---------|
| Buffer → Surface | `surface = buffer × buffer_scale` |
| Surface → Output | `output = surface × fractional_scale` |
| Buffer → Output | `output = buffer × output_scale` |

**See**: [[buffer-management#Size Relationships]]

---

## Common Patterns

### Zoom Implementation

```c
// Update zoom level
viewport.zoom *= zoom_factor;
viewport.zoom = CLAMP(viewport.zoom, 0.1, 5.0);

// Re-position all windows
wl_list_for_each(window, &windows, link) {
    int x = (window->world_x - viewport.x) * viewport.zoom;
    int y = (window->world_y - viewport.y) * viewport.zoom;
    wlr_scene_node_set_position(&window->scene_node, x, y);
}
```

**See**: [[scene-graph-scaling#Example Zoom Implementation]]

### Quality-Aware Filtering

```c
// Choose filter based on scale
enum wlr_scale_filter_mode mode;
if (is_integer_scale(scale)) {
    mode = WLR_SCALE_FILTER_NEAREST;  // Pixel perfect
} else {
    mode = WLR_SCALE_FILTER_BILINEAR; // Smooth
}
wlr_scene_buffer_set_filter_mode(buffer, mode);
```

**See**: [[scaling-algorithms#Implementation Recommendations]]

---

## Error Handling

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| Blurry text | Wrong filter mode | Use nearest for integer scales |
| Black buffers | DMA-BUF import failed | Check format/modifier support |
| Jagged edges | No fractional scale | Implement wp_fractional_scale_v1 |

**See**: [[fractional-scaling#Best Practices]], [[buffer-management#Common Pitfalls]]

---

## Related

- [[wayland-scaling-glossary]] - Term definitions
- [[implementation-guide]] - Step-by-step guide
- [[index]] - Full research vault index
