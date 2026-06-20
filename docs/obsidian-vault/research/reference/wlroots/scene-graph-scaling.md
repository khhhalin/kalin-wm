# wlroots Scene Graph API: Zoom and Scaling

**Key Terms:** [[definitions#wlr_scene|wlr_scene]] | [[definitions#wlr_scene_buffer|wlr_scene_buffer]] | [[definitions#wlr_scene_tree|wlr_scene_tree]] | [[definitions#Scene Graph|Scene Graph]] | [[definitions#wlr_scale_filter_mode|Scaling Filters]] | [[definitions#Viewport Transform|Viewport Transform]]

---

## Overview

The wlroots [[definitions#wlr_scene|scene graph API]] provides a declarative way to display [[definitions#wl_surface|surfaces]]. The compositor creates a [[definitions#Scene Graph|scene]], adds surfaces, then renders the scene on [[definitions#wlr_output|outputs]]. It only supports basic 2D composition operations (like KMS or the [[definitions#Wayland|Wayland protocol]]) - for anything more complicated, compositors need custom rendering logic.

> **Key Design Principle**: [[definitions#Scene Graph|Scene graphs]] provide features and optimizations "for free" - [[definitions#Damage Tracking|damage tracking]], visibility calculations, and protocol implementations are handled by [[definitions#wlroots|wlroots]].

---

## Core API Reference

### Scene and Scene Tree

#### `struct wlr_scene`
The root [[definitions#wlr_scene|scene graph]] node containing the main tree and output list.

```c
struct wlr_scene {
    struct wlr_scene_tree tree;     // Root tree containing all nodes
    struct wl_list outputs;         // List of wlr_scene_output
    // ... private fields
};
```

#### Creating a Scene
```c
struct wlr_scene *scene = wlr_scene_create();
```

#### `struct wlr_scene_tree`
A sub-tree in the [[definitions#Scene Graph|scene graph]] - a node that displays nothing but its children. Used for grouping and hierarchical transforms.

```c
struct wlr_scene_tree {
    struct wlr_scene_node node;     // Base node
    struct wl_list children;        // List of child nodes
};
```

#### Creating Scene Trees
```c
// Create a tree under the root
struct wlr_scene_tree *tree = wlr_scene_tree_create(&scene->tree);

// Create nested trees for hierarchical organization
struct wlr_scene_tree *workspace_tree = wlr_scene_tree_create(tree);
struct wlr_scene_tree *view_tree = wlr_scene_tree_create(workspace_tree);
```

---

### Scene Buffers and Scaling

#### `struct wlr_scene_buffer`
A [[definitions#wlr_scene_buffer|scene graph node]] displaying a [[definitions#wlr_buffer|buffer]] with configurable [[definitions#Fractional Scaling|scaling]], cropping, and [[definitions#Buffer Transform|transforms]].

```c
struct wlr_scene_buffer {
    struct wlr_scene_node node;     // Base node
    struct wlr_buffer *buffer;      // Backing buffer (can be NULL)
    
    // Public state - can be modified
    float opacity;                          // Opacity (0.0 - 1.0)
    [[definitions#wlr_scale_filter_mode|enum wlr_scale_filter_mode]] filter_mode; // Scaling filter
    struct wlr_fbox src_box;                // Source crop rectangle
    int dst_width, dst_height;              // Destination size
    [[definitions#Buffer Transform|enum wl_output_transform]] transform;     // Buffer transform
    pixman_region32_t opaque_region;        // Optimization hint
    
    // ... events and private fields
};
```

#### Creating Buffers
```c
struct wlr_scene_buffer *buffer = wlr_scene_buffer_create(parent_tree, wlr_buffer);
```

#### Key Buffer Functions for Zoom

| Function | Purpose |
|----------|---------|
| `wlr_scene_buffer_set_dest_size()` | Scale buffer to specific dimensions |
| `wlr_scene_buffer_set_source_box()` | Crop buffer before rendering |
| `wlr_scene_buffer_set_transform()` | Apply rotation/flip transforms |
| `wlr_scene_buffer_set_opacity()` | Fade in/out during zoom |
| `wlr_scene_buffer_set_filter_mode()` | Choose [[scaling-algorithms|scaling quality]] |

#### Scaling Filter Modes
```c
[[definitions#wlr_scale_filter_mode|enum wlr_scale_filter_mode]] {
    WLR_SCALE_FILTER_BILINEAR,   // [[definitions#Bilinear Filtering|Smooth scaling]] (better quality)
    WLR_SCALE_FILTER_NEAREST,    // [[definitions#Nearest Neighbor|Pixelated scaling]] (faster, retro look)
};

// Usage
wlr_scene_buffer_set_filter_mode(buffer, WLR_SCALE_FILTER_BILINEAR);
```

---

### Scene Outputs and Viewport

#### `struct wlr_scene_output`
A [[definitions#Viewport Transform|viewport]] for an [[definitions#wlr_output|output]] in the [[definitions#Scene Graph|scene graph]]. Controls where and how the scene is rendered on an output.

```c
struct wlr_scene_output {
    struct wlr_output *output;      // Associated output
    struct wlr_scene *scene;        // Parent scene
    int x, y;                       // Position in scene coordinates
    // ... damage tracking and private fields
};
```

#### Creating and Positioning Outputs
```c
// Create scene output
struct wlr_scene_output *scene_output = wlr_scene_output_create(scene, wlr_output);

// Position output in scene (useful for multi-monitor)
wlr_scene_output_set_position(scene_output, lx, ly);
```

#### Output Layout Integration
```c
// Automatically sync [[definitions#wlr_output|output]] positions with output layout
struct wlr_scene_output_layout *sol = wlr_scene_attach_output_layout(scene, output_layout);
```

---

### Scene Nodes (Base Operations)

#### `struct wlr_scene_node`
Base structure for all scene nodes.

```c
struct wlr_scene_node {
    enum wlr_scene_node_type type;       // TREE, RECT, or BUFFER
    struct wlr_scene_tree *parent;       // Parent tree
    struct wl_list link;                 // Sibling links
    bool enabled;                        // Visibility
    int x, y;                            // Position relative to parent
    // ...
};
```

#### Node Position and Hierarchy
```c
// Set position relative to parent
wlr_scene_node_set_position(&node, x, y);

// Enable/disable node and all children
wlr_scene_node_set_enabled(&node, true);

// Reparent to different tree
wlr_scene_node_reparent(&node, new_parent_tree);

// Change z-order
wlr_scene_node_raise_to_top(&node);
wlr_scene_node_lower_to_bottom(&node);
wlr_scene_node_place_above(&node, sibling);
wlr_scene_node_place_below(&node, sibling);
```

#### Coordinate Conversion
```c
// Get layout coordinates of a node
int lx, ly;
bool visible = wlr_scene_node_coords(&node, &lx, &ly);

// Find node at layout coordinates
double nx, ny;
struct wlr_scene_node *node = wlr_scene_node_at(&scene->tree.node, lx, ly, &nx, &ny);
```

---

## Nested Scene Trees for Zoom Implementation

### Hierarchical Transform Pattern

The key to implementing zoom is using nested scene trees to create hierarchical coordinate spaces:

```
Root Scene
├── Output Tree (per output)
│   ├── Background Layer
│   ├── Workspace Tree
│   │   └── Zoom Container (scaling happens here)
│   │       ├── Window 1 Tree
│   │       │   ├── Surface Buffer
│   │       │   └── Decorations
│   │       └── Window 2 Tree
│   │           └── Surface Buffer
│   └── Overlay Layer
└── Floating Tree
```

### Zoom Implementation Strategies

#### Strategy 1: Global Zoom (Entire Scene)
Scale the entire workspace by manipulating output viewport:

```c
struct zoom_state {
    struct wlr_scene_tree *zoom_tree;    // Container for all zoomable content
    float scale;                          // Current zoom level (1.0 = normal)
    int center_x, center_y;               // Zoom focus point
};

void apply_global_zoom(struct zoom_state *zoom, float new_scale, int focus_x, int focus_y) {
    float old_scale = zoom->scale;
    zoom->scale = new_scale;
    
    // Calculate offset to keep focus point stable
    int offset_x = focus_x - (focus_x * new_scale / old_scale);
    int offset_y = focus_y - (focus_y * new_scale / old_scale);
    
    // Apply scale by setting destination size on all buffers
    // Or use intermediate tree with adjusted positions
    wlr_scene_node_set_position(&zoom->zoom_tree->node, offset_x, offset_y);
}
```

#### Strategy 2: Per-Window Zoom
Scale individual windows using buffer destination size:

```c
struct view {
    struct wlr_scene_tree *tree;          // View's scene tree
    struct wlr_scene_buffer *buffer;      // Surface buffer
    float scale;
    int natural_width, natural_height;
};

void view_set_scale(struct view *view, float scale) {
    view->scale = scale;
    
    // Method A: Scale via destination size
    wlr_scene_buffer_set_dest_size(
        view->buffer,
        view->natural_width * scale,
        view->natural_height * scale
    );
    
    // Method B: Scale via source box (crop + zoom)
    struct wlr_fbox src = {
        .x = 0, .y = 0,
        .width = view->natural_width / scale,
        .height = view->natural_height / scale
    };
    wlr_scene_buffer_set_source_box(view->buffer, &src);
    wlr_scene_buffer_set_dest_size(
        view->buffer,
        view->natural_width,
        view->natural_height
    );
}
```

#### Strategy 3: Pan and Zoom Viewport
Move a virtual viewport over a larger scene:

```c
struct viewport {
    struct wlr_scene_output *output;
    float scale;
    int pan_x, pan_y;                     // Viewport origin in scene coords
};

void viewport_set_transform(struct viewport *vp, float scale, int pan_x, int pan_y) {
    vp->scale = scale;
    vp->pan_x = pan_x;
    vp->pan_y = pan_y;
    
    // Move the output within the scene to create pan effect
    wlr_scene_output_set_position(vp->output, -pan_x, -pan_y);
    
    // All content would need to be scaled appropriately
}
```

---

## Implementation Patterns

### Pattern 1: Window Zoom Animation

```c
struct animated_view {
    struct view *view;
    struct wl_event_source *animation_timer;
    float start_scale, target_scale;
    uint32_t start_time;
};

void animate_zoom(struct animated_view *av, float target_scale) {
    av->start_scale = av->view->scale;
    av->target_scale = target_scale;
    av->start_time = get_current_time_ms();
    
    // Start animation timer
    wl_event_loop_add_timer(event_loop, zoom_animation_tick, av);
}

int zoom_animation_tick(void *data) {
    struct animated_view *av = data;
    uint32_t elapsed = get_current_time_ms() - av->start_time;
    float progress = fminf(elapsed / ANIMATION_DURATION_MS, 1.0f);
    
    // Easing function
    progress = 1 - (1 - progress) * (1 - progress);  // Ease out
    
    float current_scale = av->start_scale + (av->target_scale - av->start_scale) * progress;
    view_set_scale(av->view, current_scale);
    
    if (progress < 1.0) {
        return ANIMATION_INTERVAL_MS;  // Continue animation
    }
    return 0;  // Stop animation
}
```

### Pattern 2: Scene Graph Structure for dwl-style Compositor

```c
struct server {
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;
    
    // Layer trees (following zwlr_layer_shell_v1 order)
    struct wlr_scene_tree *layer_background;
    struct wlr_scene_tree *layer_bottom;
    struct wlr_scene_tree *layer_top;
    struct wlr_scene_tree *layer_overlay;
    
    // Workspace tree (contains windows)
    struct wlr_scene_tree *workspace_tree;
    
    // Zoom container
    struct wlr_scene_tree *zoom_tree;
};

void server_init_scene(struct server *server) {
    server->scene = wlr_scene_create();
    
    // Create layer trees in z-order
    server->layer_background = wlr_scene_tree_create(&server->scene->tree);
    server->layer_bottom = wlr_scene_tree_create(&server->scene->tree);
    server->workspace_tree = wlr_scene_tree_create(&server->scene->tree);
    server->layer_top = wlr_scene_tree_create(&server->scene->tree);
    server->layer_overlay = wlr_scene_tree_create(&server->scene->tree);
    
    // Zoom container inside workspace
    server->zoom_tree = wlr_scene_tree_create(server->workspace_tree);
}

void set_zoom(struct server *server, float scale) {
    // Scale all children of zoom_tree
    struct wlr_scene_node *node;
    wl_list_for_each(node, &server->zoom_tree->children, link) {
        if (node->type == WLR_SCENE_NODE_BUFFER) {
            struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
            // Calculate new destination size based on scale
            // This requires tracking natural sizes
        }
    }
}
```

### Pattern 3: Input Coordinate Transformation

When zooming, input coordinates must be transformed:

```c
void transform_input_coords(struct zoom_state *zoom, double *x, double *y) {
    // Transform from screen coordinates to scene coordinates
    *x = (*x / zoom->scale) + zoom->offset_x;
    *y = (*y / zoom->scale) + zoom->offset_y;
}

struct wlr_surface* get_surface_at(struct server *server, double lx, double ly, 
                                    double *sx, double *sy) {
    // Transform coordinates based on zoom
    transform_input_coords(&server->zoom, &lx, &ly);
    
    // Use scene to find surface at coordinates
    struct wlr_scene_node *node = wlr_scene_node_at(
        &server->scene->tree.node, lx, ly, sx, sy);
    
    if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
        return NULL;
    }
    
    struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *surface = wlr_scene_surface_try_from_buffer(buffer);
    
    return surface ? surface->surface : NULL;
}
```

---

## [[definitions#Scene Graph|Scene Graph]] vs [[definitions#Buffer Transform|Buffer Transforms]]

### Node-Level Transforms (Position)
- **Function**: `wlr_scene_node_set_position()`
- **Effect**: Changes where the node appears in layout coordinates
- **Use Case**: Window positioning, panning, organizing content
- **Inheritance**: Child nodes inherit parent's position

### Buffer-Level Transforms (Scale/Rotate)
- **Function**: `wlr_scene_buffer_set_dest_size()`, `wlr_scene_buffer_set_transform()`
- **Effect**: Changes how the buffer content is rendered
- **Use Case**: Window resizing, zooming, rotation effects
- **Scope**: Only affects that specific buffer

### Key Differences for Zoom

| Aspect | Node Position | [[definitions#Buffer Transform|Buffer Transform]] |
|--------|---------------|------------------|
| Affects children | Yes | No |
| Coordinate space | Layout/scene | Buffer local |
| Use for pan | Yes | No |
| Use for zoom | Indirect | Direct |
| Damage tracking | Handled by wlroots | Handled by wlroots |

### Recommended Approach

For a complete zoom implementation:

1. **Use buffer destination size** for scaling individual surfaces
2. **Use node positions** for panning the viewport  
3. **Use nested trees** for organizing content into zoomable groups
4. **Transform input coordinates** to match visual representation

---

## Performance Considerations

### Automatic Optimizations (Free with Scene Graph)

#### [[definitions#Damage Tracking|Damage Tracking]]
The [[definitions#Scene Graph|scene graph]] automatically tracks [[definitions#Damage Tracking|damage]]:
```c
// No manual damage tracking needed!
// wlroots handles this internally
```

Features:
- Only redraws changed regions
- Accumulates damage across frames
- Handles transform changes automatically

#### Visibility Culling
[[definitions#Scene Graph|Scene graph]] skips rendering off-screen content:
```c
// wlroots automatically culls nodes outside output bounds
// Enabled by default, can be disabled:
scene->calculate_visibility = false;  // Rarely needed
```

#### Opaque Region Optimization
Mark opaque regions to skip rendering underneath ([[definitions#Pixman|pixman]] regions):
```c
pixman_region32_t opaque;
pixman_region32_init_rect(&opaque, 0, 0, width, height);
wlr_scene_buffer_set_opaque_region(buffer, &opaque);
pixman_region32_fini(&opaque);
```

### Performance Best Practices

#### 1. Minimize Scene Graph Depth
```c
// Good: Flat hierarchy
Root -> Workspace -> Windows

// Avoid: Deep nesting
Root -> Output -> Workspace -> Container -> Subcontainer -> Windows
```

#### 2. Use Filter Mode Appropriately
```c
// For pixel-perfect content (retro games, terminal)
wlr_scene_buffer_set_filter_mode(buffer, [[definitions#Nearest Neighbor|WLR_SCALE_FILTER_NEAREST]]);

// For [[definitions#Bilinear Filtering|smooth scaling]] (images, video)
wlr_scene_buffer_set_filter_mode(buffer, [[definitions#Bilinear Filtering|WLR_SCALE_FILTER_BILINEAR]]);
```

#### 3. Batch Position Updates
```c
// Avoid: Setting positions one at a time with intermediate renders
// Better: Set all positions, then render once
```

#### 4. Monitor Performance
```c
// Get render timing information
struct wlr_scene_timer timer = {0};
struct wlr_scene_output_state_options options = {
    .timer = &timer,
};
wlr_scene_output_commit(scene_output, &options);

int64_t duration = wlr_scene_timer_get_duration_ns(&timer);
```

### Memory Considerations

#### Scene Tree Overhead
- Each node has overhead (~few hundred bytes)
- Keep node count reasonable (hundreds, not thousands)
- Reuse trees rather than recreating

#### Buffer Management
```c
// Scene buffer doesn't own the wlr_buffer
// Compositor manages buffer lifecycle
wlr_scene_buffer_set_buffer(scene_buffer, buffer);
// Can set to NULL when buffer not needed
wlr_scene_buffer_set_buffer(scene_buffer, NULL);
```

---

## Real-World Examples

### labwc Scene Graph Structure

[[zoom-features#labwc|labwc]] uses scene graph extensively (as of 0.8+):

```
Scene
├── Layer Tree (background/bottom/top/overlay)
├── Workspace Tree
│   ├── SSD Tree (server-side decorations)
│   │   ├── Titlebar
│   │   └── Borders
│   └── Surface Tree
├── Menu Tree
└── OSD Tree (on-screen display)
```

Key patterns from labwc:
- Separate trees for different layer shell layers
- SSD as sibling to surface tree
- Menu and OSD on separate trees for z-order control

### [[zoom-features#Sway|Sway's]] Proposed Scene Graph Structure

```
Root
├── Output 1
│   ├── Background/bottom layers
│   ├── Workspace 1 (enabled when focused)
│   │   ├── View container (offset for container coords)
│   │   │   ├── Borders (rect nodes)
│   │   │   ├── View 1 (surface + subsurfaces)
│   │   │   └── View 2
│   │   └── Popups container
│   └── Top/overlay layers
└── Output 2
    └── ...
```

### [[zoom-features#dwl|dwl]] with Scene Graph

dwl (as of recent versions) uses scene graph for damage tracking:

```c
// dwl creates scene trees for:
// - Layers (background, bottom, top, overlay)
// - Clients (xdg and xwayland)
// - Drag icons

struct Monitor {
    struct wlr_scene_output *scene_output;
    // ...
};

void drawbar(Monitor *m) {
    // Uses scene rects for bar elements
    struct wlr_scene_rect *rect = wlr_scene_rect_create(
        bar->scene_tree, width, height, color);
}
```

---

## Code Example: Complete Zoom Implementation

```c
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>

struct zoom_compositor {
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;
    
    // Zoom state
    struct wlr_scene_tree *zoom_container;
    float zoom_scale;
    int zoom_center_x, zoom_center_y;
    
    // Original positions for reset
    struct wl_list views;  // struct zoom_view
};

struct zoom_view {
    struct wl_list link;
    struct wlr_scene_tree *tree;
    struct wlr_scene_buffer *buffer;
    int natural_width, natural_height;
    int scene_x, scene_y;
};

void zoom_compositor_init(struct zoom_compositor *zc, struct wlr_output_layout *layout) {
    zc->scene = wlr_scene_create();
    zc->scene_layout = wlr_scene_attach_output_layout(zc->scene, layout);
    zc->zoom_scale = 1.0f;
    
    // Create zoom container
    zc->zoom_container = wlr_scene_tree_create(&zc->scene->tree);
    
    wl_list_init(&zc->views);
}

void zoom_view_create(struct zoom_compositor *zc, struct wlr_surface *surface) {
    struct zoom_view *view = calloc(1, sizeof(*view));
    
    // Create view tree inside zoom container
    view->tree = wlr_scene_tree_create(zc->zoom_container);
    
    // Create buffer for surface
    view->buffer = wlr_scene_buffer_create(view->tree, NULL);
    struct wlr_scene_surface *scene_surface = 
        wlr_scene_surface_create(view->tree, surface);
    
    // Store natural size
    view->natural_width = surface->current.width;
    view->natural_height = surface->current.height;
    
    wl_list_insert(&zc->views, &view->link);
}

void apply_zoom(struct zoom_compositor *zc, float new_scale) {
    float old_scale = zc->zoom_scale;
    zc->zoom_scale = new_scale;
    
    struct zoom_view *view;
    wl_list_for_each(view, &zc->views, link) {
        // Calculate new size
        int new_width = view->natural_width * new_scale;
        int new_height = view->natural_height * new_scale;
        
        // Apply destination size
        wlr_scene_buffer_set_dest_size(view->buffer, new_width, new_height);
        
        // Adjust position to maintain relative layout
        // This is simplified - real implementation needs proper layout logic
        int new_x = view->scene_x * (new_scale / old_scale);
        int new_y = view->scene_y * (new_scale / old_scale);
        wlr_scene_node_set_position(&view->tree->node, new_x, new_y);
    }
}

void zoom_to_point(struct zoom_compositor *zc, float new_scale, int focus_x, int focus_y) {
    float ratio = new_scale / zc->zoom_scale;
    
    // Calculate pan offset to keep focus point stable
    int container_x, container_y;
    wlr_scene_node_coords(&zc->zoom_container->node, &container_x, &container_y);
    
    int new_container_x = focus_x - (focus_x - container_x) * ratio;
    int new_container_y = focus_y - (focus_y - container_y) * ratio;
    
    wlr_scene_node_set_position(&zc->zoom_container->node, new_container_x, new_container_y);
    
    apply_zoom(zc, new_scale);
}

void render_output(struct zoom_compositor *zc, struct wlr_output *output) {
    struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(zc->scene, output);
    
    // Render the scene
    wlr_scene_output_commit(scene_output, NULL);
    
    // Send frame done to surfaces
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}
```

---

## References

### Official Documentation
- [wlroots Scene API](https://wlroots.pages.freedesktop.org/wlroots/wlr/types/wlr_scene.h.html)
- [wlroots GitLab](https://gitlab.freedesktop.org/wlroots/wlroots)

### Example Compositors Using Scene Graph
- [[zoom-features#labwc|**labwc**]]: Full-featured stacking compositor using scene graph
- [[zoom-features#dwl|**dwl**]]: Minimal tiling compositor with scene graph damage tracking
- [[zoom-features#River|**river**]]: Tiling compositor migrated to scene graph in 0.3.0

### Related Projects
- **scenefx**: Drop-in replacement with eye-candy effects (blur, shadows, rounded corners)

---

## Summary

The wlroots scene graph API provides a powerful foundation for implementing zoom and scaling:

1. **Use `wlr_scene_buffer_set_dest_size()`** for scaling individual surfaces
2. **Use `wlr_scene_node_set_position()`** for positioning and panning
3. **Use nested `wlr_scene_tree`** for organizing content hierarchically
4. **Transform input coordinates** to match the visual zoom level
5. **Leverage automatic optimizations** like damage tracking and visibility culling

The declarative nature of the [[definitions#Scene Graph|scene graph]] means the compositor describes what should be rendered where, and [[definitions#wlroots|wlroots]] handles the efficient rendering details.
