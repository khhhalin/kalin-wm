# wlroots Fractional Scaling and Buffer Scaling APIs

> Research document covering `wp_fractional_scale_v1`, [[scene-graph-scaling|wlr_scene]] buffer scaling, and best practices for crisp rendering in wlroots-based Wayland compositors.

---

## Table of Contents

1. [Overview](#overview)
2. [wp_fractional_scale_v1 Protocol](#wp_fractional_scale_v1-protocol)
3. [[scene-graph-scaling#wlr_scene Buffer Scaling API|wlr_scene Buffer Scaling API]]
4. [Buffer Scale vs Output Scale](#buffer-scale-vs-output-scale)
5. [Internal Fractional Scaling Implementation](#internal-fractional-scaling-implementation)
6. [[zoom-features#Sway and Niri Implementation Approaches|Sway and Niri Implementation Approaches]]
7. [Best Practices for Crisp Scaling](#best-practices-for-crisp-scaling)
8. [Code Examples](#code-examples)
9. [References](#references)

---

## Overview

Fractional scaling in Wayland allows compositors to display content at non-integer scale factors (e.g., 1.5x, 1.25x) on high-DPI displays. wlroots provides comprehensive support through:

- **`wp_fractional_scale_v1`**: Wayland protocol for communicating preferred fractional scales to clients
- **`wlr_scene`**: [[scene-graph-scaling|Scene-graph API]] that handles scaling, transforms, and filtering automatically
- **[[buffer-management|Buffer management]]**: APIs for setting buffer scale and transform on surfaces

The key insight is that fractional scaling requires **client-side cooperation** - clients must render at higher resolutions and use `wp_viewport` to communicate the intended destination size.

---

## wp_fractional_scale_v1 Protocol

### Protocol Basics

The fractional scale protocol allows compositors to suggest fractional scales to clients. The protocol uses a **v120 encoding** where scale values are multiplied by 120.

```c
// From fractional-scale-v1-protocol.h
// Scale 1.5 = 180 (1.5 * 120)
// Scale 2.0 = 240 (2.0 * 120)
// Scale 1.25 = 150 (1.25 * 120)

#define WP_FRACTIONAL_SCALE_V1_PREFERRED_SCALE_SINCE_VERSION 1

static inline void
wp_fractional_scale_v1_send_preferred_scale(struct wl_resource *resource_, uint32_t scale)
{
    wl_resource_post_event(resource_, WP_FRACTIONAL_SCALE_V1_PREFERRED_SCALE, scale);
}
```

### wlroots API

```c
// From include/wlr/types/wlr_fractional_scale_v1.h

struct wlr_fractional_scale_manager_v1 {
    struct wl_global *global;
    struct {
        struct wl_signal destroy;
    } events;
    // ... private fields
};

// Create the global manager
struct wlr_fractional_scale_manager_v1 *wlr_fractional_scale_manager_v1_create(
    struct wl_display *display, uint32_t version);

// Notify a surface of its preferred fractional scale
void wlr_fractional_scale_v1_notify_scale(
    struct wlr_surface *surface, double scale);
```

### Implementation Details

From `types/wlr_fractional_scale_v1.c`:

```c
// v120 conversion - rounds to nearest 1/120th
static uint32_t double_to_v120(double d) {
    return round(d * 120);
}

void wlr_fractional_scale_v1_notify_scale(struct wlr_surface *surface, double scale) {
    struct wlr_addon *addon = wlr_addon_find(&surface->addons,
        NULL, &fractional_scale_addon_impl);

    if (addon == NULL) {
        // Create a dummy object to store the scale
        struct wlr_fractional_scale_v1 *info = calloc(1, sizeof(*info));
        wlr_addon_init(&info->addon, &surface->addons, NULL, &fractional_scale_addon_impl);
        info->scale = scale;
        return;
    }

    struct wlr_fractional_scale_v1 *info = wl_container_of(addon, info, addon);
    if (info->scale == scale) {
        return;  // No change, skip notification
    }

    info->scale = scale;

    if (!info->resource) {
        // Update existing dummy object (client hasn't bound fractional scale yet)
        return;
    }

    // Send the preferred scale event to client
    wp_fractional_scale_v1_send_preferred_scale(info->resource, double_to_v120(scale));
}
```

### Client Workflow

1. Client binds `wp_fractional_scale_manager_v1`
2. Client creates `wp_fractional_scale_v1` for a surface via `get_fractional_scale`
3. Compositor sends `preferred_scale` event with v120-encoded value
4. Client renders buffer at: `surface_size * scale / 120`
5. Client sets viewport destination to `surface_size` (pre-scale dimensions)
6. Client keeps `wl_surface.set_buffer_scale` at 1

---

## wlr_scene Buffer Scaling API

### Key Structures

```c
// From include/wlr/types/wlr_scene.h

struct wlr_scene_buffer {
    struct wlr_scene_node node;
    struct wlr_buffer *buffer;  // May be NULL

    // Primary output this buffer is displayed on
    struct wlr_scene_output *primary_output;

    // Rendering properties
    float opacity;
    enum wlr_scale_filter_mode filter_mode;
    struct wlr_fbox src_box;    // Source crop rectangle
    int dst_width, dst_height;  // Destination size (for scaling)
    enum wl_output_transform transform;
    pixman_region32_t opaque_region;
    
    // ... color management fields
};

struct wlr_scene_surface {
    struct wlr_scene_buffer *buffer;
    struct wlr_surface *surface;
    // ... private fields
};
```

### Buffer Scaling Functions

```c
// Set destination size for scaling
// If width/height are 0, uses buffer size (no scaling)
void wlr_scene_buffer_set_dest_size(struct wlr_scene_buffer *scene_buffer,
    int width, int height);

// Set source crop rectangle
void wlr_scene_buffer_set_source_box(struct wlr_scene_buffer *scene_buffer,
    const struct wlr_fbox *box);

// Set transform (rotation/flip)
void wlr_scene_buffer_set_transform(struct wlr_scene_buffer *scene_buffer,
    enum wl_output_transform transform);

// Set scaling filter mode
void wlr_scene_buffer_set_filter_mode(struct wlr_scene_buffer *scene_buffer,
    enum wlr_scale_filter_mode filter_mode);
```

### Scale Filter Modes

```c
// From wl_output_transform in wayland-server-protocol.h
typedef enum {
    WL_OUTPUT_TRANSFORM_NORMAL = 0,       // No transform
    WL_OUTPUT_TRANSFORM_90 = 1,           // 90 degrees clockwise
    WL_OUTPUT_TRANSFORM_180 = 2,          // 180 degrees
    WL_OUTPUT_TRANSFORM_270 = 3,          // 270 degrees clockwise
    WL_OUTPUT_TRANSFORM_FLIPPED = 4,      // Flipped horizontally
    WL_OUTPUT_TRANSFORM_FLIPPED_90 = 5,   // Flipped + 90 degrees
    WL_OUTPUT_TRANSFORM_FLIPPED_180 = 6,  // Flipped vertically
    WL_OUTPUT_TRANSFORM_FLIPPED_270 = 7,  // Flipped + 270 degrees
} wl_output_transform;
```

### wlr_scene_surface Auto-Configuration

When using `wlr_scene_surface_create()`, wlroots automatically handles:

1. **wp_viewporter**: Applies viewport source/destination
2. **wp_fractional_scale_v1**: Sends preferred scale
3. **wl_surface.set_buffer_scale**: Sends integer scale (ceil of fractional)
4. **wl_surface.set_buffer_transform**: Sends preferred transform
5. **Output enter/leave**: Tracks which outputs surface is on

From `types/scene/surface.c`:

```c
static void handle_scene_buffer_outputs_update(
        struct wl_listener *listener, void *data) {
    struct wlr_scene_surface *surface = wl_container_of(listener, surface, outputs_update);
    struct wlr_scene_outputs_update_event *event = data;
    
    // ... output enter/leave handling ...
    
    // Calculate preferred buffer scale from outputs
    double scale = get_surface_preferred_buffer_scale(surface->surface);
    
    // Send fractional scale
    wlr_fractional_scale_v1_notify_scale(surface->surface, scale);
    
    // Send integer buffer scale (ceil for compatibility)
    wlr_surface_set_preferred_buffer_scale(surface->surface, ceil(scale));
    
    // ... color management ...
}

static double get_surface_preferred_buffer_scale(struct wlr_surface *surface) {
    double scale = 1;
    struct wlr_surface_output *surface_output;
    wl_list_for_each(surface_output, &surface->current_outputs, link) {
        if (surface_output->output->scale > scale) {
            scale = surface_output->output->scale;
        }
    }
    return scale;
}
```

### Surface Reconfiguration

The `surface_reconfigure()` function in `types/scene/surface.c` shows how wlroots applies all scaling parameters:

```c
static void surface_reconfigure(struct wlr_scene_surface *scene_surface) {
    struct wlr_scene_buffer *scene_buffer = scene_surface->buffer;
    struct wlr_surface *surface = scene_surface->surface;
    struct wlr_surface_state *state = &surface->current;

    // Get source box (handles viewport source cropping)
    struct wlr_fbox src_box;
    wlr_surface_get_buffer_source_box(surface, &src_box);

    // Surface size in surface-local coordinates
    int width = state->width;
    int height = state->height;

    // Apply clipping if set
    if (!wlr_box_empty(&scene_surface->clip)) {
        // ... clip calculations ...
    }

    // Apply alpha modifier
    float opacity = 1.0;
    const struct wlr_alpha_modifier_surface_v1_state *alpha_modifier_state =
        wlr_alpha_modifier_v1_get_surface_state(surface);
    if (alpha_modifier_state != NULL) {
        opacity = (float)alpha_modifier_state->multiplier;
    }

    // Configure the scene buffer
    wlr_scene_buffer_set_opaque_region(scene_buffer, &opaque);
    wlr_scene_buffer_set_source_box(scene_buffer, &src_box);
    wlr_scene_buffer_set_dest_size(scene_buffer, width, height);
    wlr_scene_buffer_set_transform(scene_buffer, state->transform);
    wlr_scene_buffer_set_opacity(scene_buffer, opacity);
    // ... color management ...

    // Set the buffer with damage tracking
    if (surface->buffer) {
        wlr_scene_buffer_set_buffer_with_options(scene_buffer,
            &surface->buffer->base, &options);
    } else {
        wlr_scene_buffer_set_buffer(scene_buffer, NULL);
    }
}
```

---

## Buffer Scale vs Output Scale

### Key Concepts

| Concept | Description | API |
|---------|-------------|-----|
| **Output Scale** | Scale factor of the physical display | `wlr_output.state.scale` (float) |
| **Buffer Scale** | Integer scale hint for clients | `wl_surface.set_buffer_scale` (int) |
| **Fractional Scale** | Precise fractional scale | `wp_fractional_scale_v1.preferred_scale` (v120) |
| **Viewport Scale** | Applied via viewporter destination | `wp_viewport.set_destination` |

### The Scaling Pipeline

```
Client Buffer (buffer coordinates)
    ↓
Buffer Scale (integer division: buffer_size / scale)
    ↓
Buffer Transform (rotation/flip applied)
    ↓
Viewport Source (crop rectangle, optional)
    ↓
Viewport Destination (scale to output size)
    ↓
Output Scale (final compositor scaling to physical pixels)
    ↓
Physical Display
```

### When to Use Each

**Buffer Scale (`wl_surface.set_buffer_scale`)**:
- Legacy integer-only scaling
- Clients render at `buffer_scale * surface_size`
- Used when fractional_scale_v1 is not available

**Fractional Scale (`wp_fractional_scale_v1`)**:
- Modern precise fractional scaling
- Clients render at `fractional_scale * surface_size`
- Uses viewporter for final sizing
- wl_surface.buffer_scale stays at 1

**Output Scale (`wlr_output.state.scale`)**:
- Configured by user (e.g., `output HDMI-A-1 scale 1.5`)
- Drives fractional_scale notifications
- Scene graph handles final rendering

---

## Internal Fractional Scaling Implementation

### Scene Graph Rendering

The scene graph in `types/scene/wlr_scene.c` handles scaling during rendering:

```c
// From scene_entry_render() in wlr_scene.c
static void scene_entry_render(struct render_list_entry *entry, const struct render_data *data) {
    // ... setup ...

    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
    
    // Get texture from buffer
    struct wlr_texture *texture = scene_buffer_get_texture(scene_buffer,
        data->output->output->renderer);
    
    // Calculate transform
    enum wl_output_transform transform =
        wlr_output_transform_invert(scene_buffer->transform);
    transform = wlr_output_transform_compose(transform, data->transform);

    // Render with proper scaling
    wlr_render_pass_add_texture(data->render_pass, &(struct wlr_render_texture_options) {
        .texture = texture,
        .src_box = scene_buffer->src_box,
        .dst_box = dst_box,
        .transform = transform,
        .clip = &render_region,
        .alpha = &scene_buffer->opacity,
        .filter_mode = scene_buffer->filter_mode,
        // ... other options
    });
}
```

### Damage Tracking with Scaling

Damage must account for scaling factors. From `wlr_scene.c`:

```c
static void scale_region(pixman_region32_t *region, float scale, bool round_up) {
    wlr_region_scale(region, region, scale);
    
    if (round_up && floor(scale) != scale) {
        // For fractional scales, expand damage by 1 pixel to account
        // for rounding errors
        wlr_region_expand(region, region, 1);
    }
}

static void logical_to_buffer_coords(pixman_region32_t *region, 
        const struct render_data *data, bool round_up) {
    enum wl_output_transform transform = wlr_output_transform_invert(data->transform);
    scale_region(region, data->scale, round_up);
    wlr_region_transform(region, region, transform, data->trans_width, data->trans_height);
}
```

### Buffer Damage Expansion

When a buffer is updated, damage must be expanded for scaling:

```c
// From wlr_scene_buffer_set_buffer_with_options()
// Expand damage for scaling artifacts
float buffer_scale_x = 1.0f / output_scale_x;
float buffer_scale_y = 1.0f / output_scale_y;
int dist_x = floor(buffer_scale_x) != buffer_scale_x ?
    (int)ceilf(output_scale_x / 2.0f) : 0;
int dist_y = floor(buffer_scale_y) != buffer_scale_y ?
    (int)ceilf(output_scale_y / 2.0f) : 0;

// Expand damage region to account for filtering
wlr_region_expand(&output_damage, &output_damage,
    dist_x >= dist_y ? dist_x : dist_y);
```

---

## Sway and Niri Implementation Approaches

### Sway's Approach

[[zoom-features#Sway|Sway]] uses wlroots' scene graph API (`wlr_scene`) which handles most fractional scaling automatically:

1. **Configuration**: User sets `output <name> scale <factor>`
2. **Output scale**: Applied via `wlr_output_state_set_scale()`
3. **Surface notifications**: `wlr_scene_surface` handles `wp_fractional_scale_v1` and `wl_surface.set_buffer_scale`
4. **Rendering**: Scene graph handles all scaling/filtering

Key implementation in Sway:

```c
// Sway's output configuration applies scale
struct wlr_output_state state;
wlr_output_state_init(&state);
wlr_output_state_set_scale(&state, output->scale);
wlr_output_state_set_transform(&state, output->transform);
wlr_output_commit_state(wlr_output, &state);
wlr_output_state_finish(&state);
```

### Niri's Approach

[[zoom-features#Niri|Niri]] (Rust-based using Smithay) has similar concepts but implements fractional scaling with attention to:

1. **Crisp rendering**: Careful handling of buffer sizes to avoid blur
2. **Xwayland**: No native fractional scaling (blurry), but supports gamescope workaround
3. **Viewporter**: Heavy use of viewport for precise sizing

Niri release notes highlight:
> "Fixed lock screen surface viewporter support (used for fractional scaling)"

### Key Differences

| Aspect | Sway (wlroots scene) | Niri (Smithay) |
|--------|---------------------|----------------|
| Scaling API | `wlr_scene` handles automatically | Manual viewport management |
| Filter mode | Configurable (linear/nearest) | Linear default |
| Xwayland | Built-in (can be blurry) | No native support (use gamescope) |
| Damage tracking | Automatic | Manual implementation |

---

## Best Practices for Crisp Scaling

### 1. Use wlr_scene for Automatic Handling

```c
// Create scene and surface - handles fractional scale automatically
struct wlr_scene *scene = wlr_scene_create();
struct wlr_scene_surface *scene_surface = wlr_scene_surface_create(
    parent_tree, wlr_surface);

// Enable protocols
wlr_fractional_scale_manager_v1_create(display, 1);
wlr_viewporter_create(display);
```

### 2. Configure Output Scale Correctly

```c
void configure_output(struct wlr_output *output, float scale) {
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_scale(&state, scale);
    
    // Get effective resolution
    int width, height;
    wlr_output_effective_resolution(output, &width, &height);
    
    wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);
}
```

### 3. Handle Multiple Outputs with Different Scales

```c
// wlr_scene automatically picks the largest scale from visible outputs
double get_surface_preferred_buffer_scale(struct wlr_surface *surface) {
    double scale = 1;
    struct wlr_surface_output *surface_output;
    wl_list_for_each(surface_output, &surface->current_outputs, link) {
        if (surface_output->output->scale > scale) {
            scale = surface_output->output->scale;
        }
    }
    return scale;
}
```

### 4. Choose Appropriate Filter Modes

```c
// For UI/text: Linear is usually best (smoother)
wlr_scene_buffer_set_filter_mode(scene_buffer, WLR_SCALE_FILTER_LINEAR);

// For pixel art/integer scaling: Nearest neighbor (sharper)
wlr_scene_buffer_set_filter_mode(scene_buffer, WLR_SCALE_FILTER_NEAREST);

// Note: WLR_SCALE_FILTER_BILINEAR may also be available depending on wlroots version
```

See [[scaling-algorithms]] for detailed information on scaling algorithms including [[scaling-algorithms#AMD FidelityFX Super Resolution 1.0 (FSR)|FSR]] and [[scaling-algorithms#NVIDIA Image Scaling (NIS)|NIS]].

### 5. Handle Damage Correctly

```c
// Damage in surface-local coordinates
pixman_region32_t damage;
pixman_region32_init_rect(&damage, x, y, width, height);

// Scale damage to output coordinates
wlr_region_scale(&damage, &damage, output->scale);

// For fractional scales, expand by 1 pixel
if (floor(output->scale) != output->scale) {
    wlr_region_expand(&damage, &damage, 1);
}

wlr_output_state_set_damage(&state, &damage);
```

### 6. Support Both Legacy and Modern Protocols

```c
// Send both fractional scale (new) and buffer scale (legacy)
void notify_scale(struct wlr_surface *surface, double scale) {
    // Modern: fractional scale via v120 encoding
    wlr_fractional_scale_v1_notify_scale(surface, scale);
    
    // Legacy: integer buffer scale (ceiling)
    wlr_surface_set_preferred_buffer_scale(surface, ceil(scale));
    
    // Transform (for rotated displays)
    wlr_surface_set_preferred_buffer_transform(surface, output->transform);
}
```

### 7. Testing Crispness

To verify fractional scaling is working correctly:

1. **Text clarity**: Terminal/text editor at fractional scale should be crisp
2. **No blur with native clients**: Firefox, Chromium with Wayland backend
3. **Damage tracking**: No visual artifacts when windows update
4. **Multi-monitor**: Move window between different scales - should adapt

---

## Code Examples

### Complete Minimal Example

```c
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_compositor.h>

struct compositor {
    struct wl_display *display;
    struct wlr_scene *scene;
    struct wlr_fractional_scale_manager_v1 *fractional_scale;
    struct wlr_viewporter *viewporter;
};

bool compositor_init(struct compositor *comp, struct wl_display *display) {
    comp->display = display;
    
    // Create scene graph
    comp->scene = wlr_scene_create();
    if (!comp->scene) {
        return false;
    }
    
    // Enable fractional scale protocol
    comp->fractional_scale = wlr_fractional_scale_manager_v1_create(display, 1);
    if (!comp->fractional_scale) {
        return false;
    }
    
    // Enable viewporter (required for fractional scaling)
    comp->viewporter = wlr_viewporter_create(display);
    if (!comp->viewporter) {
        return false;
    }
    
    return true;
}

// Create a surface in the scene - handles fractional scale automatically
struct wlr_scene_surface *create_scene_surface(
        struct compositor *comp,
        struct wlr_scene_tree *parent,
        struct wlr_surface *surface) {
    
    // This automatically:
    // - Sets up fractional scale notifications
    // - Handles viewport changes
    // - Manages buffer scale/transform
    return wlr_scene_surface_create(parent, surface);
}

// Configure output with fractional scale
void configure_output_scale(struct wlr_output *output, float scale) {
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    
    wlr_output_state_set_scale(&state, scale);
    
    // Calculate mode for scaled resolution
    int width = output->width / output->scale * scale;
    int height = output->height / output->scale * scale;
    
    wlr_output_state_set_custom_mode(&state, width, height, output->refresh);
    
    if (!wlr_output_test_state(output, &state)) {
        wlr_log(WLR_ERROR, "Output configuration test failed");
        wlr_output_state_finish(&state);
        return;
    }
    
    wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);
}
```

### Manual Buffer Scaling (without wlr_scene)

If not using `wlr_scene`, you must handle scaling manually:

```c
void render_surface(struct wlr_surface *surface, 
        struct wlr_output *output, 
        struct wlr_render_pass *pass,
        int x, int y, int width, int height) {
    
    struct wlr_texture *texture = wlr_surface_get_texture(surface);
    if (!texture) return;
    
    // Get source box (handles viewport cropping)
    struct wlr_fbox src_box;
    wlr_surface_get_buffer_source_box(surface, &src_box);
    
    // Destination box in output coordinates
    struct wlr_box dst_box = {
        .x = x,
        .y = y,
        .width = width,
        .height = height
    };
    
    // Scale destination to output coordinates
    dst_box.x *= output->scale;
    dst_box.y *= output->scale;
    dst_box.width *= output->scale;
    dst_box.height *= output->scale;
    
    // Calculate transform
    enum wl_output_transform transform =
        wlr_output_transform_invert(surface->current.transform);
    transform = wlr_output_transform_compose(transform, output->transform);
    
    // Render
    wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options) {
        .texture = texture,
        .src_box = src_box,
        .dst_box = dst_box,
        .transform = transform,
        .filter_mode = WLR_SCALE_FILTER_LINEAR,
        .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
    });
}
```

### Handling Mixed DPI Setups

```c
// Per-output rendering with proper scaling
void render_output(struct wlr_output *output, struct wlr_scene *scene) {
    struct wlr_scene_output *scene_output = 
        wlr_scene_get_scene_output(scene, output);
    
    struct wlr_scene_output_state_options options = {
        .timer = NULL,
        // Can specify custom color transform here
        .color_transform = NULL,
    };
    
    // wlr_scene handles all the scaling complexities
    if (!wlr_scene_output_commit(scene_output, &options)) {
        wlr_log(WLR_ERROR, "Failed to render output");
    }
}
```

---

## References

### Source Files

| File | Description |
|------|-------------|
| `include/wlr/types/wlr_fractional_scale_v1.h` | Fractional scale protocol API |
| `include/wlr/types/wlr_scene.h` | Scene graph API |
| `include/wlr/types/wlr_compositor.h` | Surface and buffer management |
| `include/wlr/types/wlr_viewporter.h` | Viewport protocol API |
| `types/wlr_fractional_scale_v1.c` | Protocol implementation |
| `types/scene/wlr_scene.c` | Scene graph implementation |
| `types/scene/surface.c` | Scene surface handling |
| `types/wlr_compositor.c` | Core surface logic |
| `types/wlr_viewporter.c` | Viewport implementation |

### Wayland Protocols

- [fractional-scale-v1](https://wayland.app/protocols/fractional-scale-v1) - Official protocol spec
- [viewporter](https://wayland.app/protocols/viewporter) - Required for fractional scaling

### External Resources

- [Sway fractional scaling discussion](https://github.com/swaywm/sway/issues/8117) - Crispness issues with Qt6
- [Niri releases](https://github.com/niri-wm/niri/releases) - Fractional scaling changelog entries
- [wlroots 0.17 release notes](https://www.phoronix.com/news/wlroots-0.17) - Initial fractional scale support
- [Fractional Scaling Protocol MR](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/merge_requests/297) - Protocol design discussion

---

## Summary

wlroots provides comprehensive fractional scaling support through:

1. **wp_fractional_scale_v1**: Modern protocol for precise fractional scale communication (v120 encoding)
2. **wlr_scene**: High-level API that handles all scaling automatically
3. **Legacy support**: Buffer scale for older clients

Best practices:
- Use `wlr_scene` when possible for automatic handling
- Enable both `wp_fractional_scale_v1` and `wp_viewporter`
- Send both fractional and integer scale hints for compatibility
- Use appropriate filter modes (linear for UI, nearest for pixel art)
- Expand damage regions for fractional scales to avoid artifacts

---

## Related Topics

- [[scene-graph-scaling|wlroots Scene Graph API]] - Complete guide to wlr_scene for zoom and scaling
- [[buffer-management|Buffer Management and DMA-BUF]] - Zero-copy buffer sharing and resolution independence
- [[scaling-algorithms|GPU Scaling Algorithms]] - FSR, NIS, Lanczos, and filtering modes
- [[zoom-features|Compositor Zoom Features]] - Comparison of zoom implementations in Hyprland, Niri, Sway, and others
