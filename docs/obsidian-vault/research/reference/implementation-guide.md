# Implementation Guide

> Step-by-step guide for implementing robust zoom and scaling in kalin-wm.

---

## Prerequisites

Before starting, understand:
- [[fractional-scaling|Fractional scaling concepts]]
- [[scene-graph-scaling|wlroots scene graph API]]
- [[wayland-scaling-glossary#Viewport Transform|Coordinate systems]]

**Required Reading**: 
1. [[scene-graph-scaling#Overview]] - Scene graph basics
2. [[fractional-scaling#Buffer Scale vs Output Scale]] - Scale concepts
3. [[buffer-management#Size Relationships]] - Coordinate transforms

---

## Architecture Decision

Choose your approach:

### Option A: Scene Graph Based (Recommended)

**Pros:**
- Automatic damage tracking
- Built-in visibility culling
- Protocol implementations handled
- Easier to maintain

**Cons:**
- Limited to basic 2D operations
- Less control over rendering

**Best for:** Most compositors, including kalin-wm

**See**: [[scene-graph-scaling]], [[zoom-features#Niri]]

### Option B: Custom Renderer

**Pros:**
- Full control over rendering
- Can implement advanced shaders (FSR)
- More flexible

**Cons:**
- Must implement damage tracking
- More code to maintain
- Complex protocol integration

**Best for:** Advanced compositors with unique rendering needs

**See**: [[scaling-algorithms#wlroots Integration]], [[zoom-features#Wayfire]]

### Recommendation for kalin-wm

Use **Option A (Scene Graph)** with plans to migrate to **Option B** for advanced filters in Phase 4.

---

## Phase 1: Basic Infrastructure

### 1.1 Set Up Fractional Scale Support

```c
// In setup() or main()
#include <wlr/types/wlr_fractional_scale_v1.h>

struct wlr_fractional_scale_manager_v1 *fractional_scale_mgr;

void setup_fractional_scale(struct wl_display *display) {
    // Create the global manager (version 1)
    fractional_scale_mgr = wlr_fractional_scale_manager_v1_create(display, 1);
    if (!fractional_scale_mgr) {
        wlr_log(WLR_ERROR, "Failed to create fractional scale manager");
    }
}
```

**See**: [[fractional-scaling#wlroots API]]

### 1.2 Configure Scene Graph Layers

```c
// Define layer enum for organization
enum layers {
    LYR_BG,      // Background/wallpaper
    LYR_TILE,    // Tiled windows
    LYR_FLOAT,   // Floating windows
    LYR_TOP,     // Top layer (notifications)
    LYR_OVERLAY, // Overlay (crop UI)
    NUM_LAYERS
};

struct wlr_scene_tree *layers[NUM_LAYERS];

void setup_scene_layers(struct wlr_scene *scene) {
    for (int i = 0; i < NUM_LAYERS; i++) {
        layers[i] = wlr_scene_tree_create(&scene->tree);
    }
}
```

**See**: [[scene-graph-scaling#Creating Scene Trees]]

### 1.3 Create Zoom Container Tree

```c
// This tree will contain all windows and be transformed for zoom
struct wlr_scene_tree *zoom_container;

void setup_zoom_container(struct wlr_scene *scene) {
    // Create under tile layer - all tiled windows go here
    zoom_container = wlr_scene_tree_create(layers[LYR_TILE]);
}
```

**See**: [[scene-graph-scaling#Nested Scene Trees for Zoom]]

---

## Phase 2: Basic Zoom Implementation

### 2.1 Viewport State Structure

```c
struct viewport {
    float x, y;      // Camera position in world coordinates
    float zoom;      // Zoom factor (1.0 = normal)
    bool follow;     // Follow focused window
} viewport = {0, 0, 1.0, false};
```

**See**: [[wayland-scaling-glossary#Viewport Transform]]

### 2.2 Coordinate Transform Functions

```c
// World → Screen (with zoom)
static inline int world_to_screen_x(float world_x) {
    return (int)((world_x - viewport.x) * viewport.zoom);
}

static inline int world_to_screen_y(float world_y) {
    return (int)((world_y - viewport.y) * viewport.zoom);
}

// Screen → World (for mouse input)
static inline float screen_to_world_x(int screen_x) {
    return (screen_x / viewport.zoom) + viewport.x;
}

static inline float screen_to_world_y(int screen_y) {
    return (screen_y / viewport.zoom) + viewport.y;
}
```

**See**: [[scene-graph-scaling#Coordinate Transformations]]

### 2.3 Pan Implementation

```c
void viewport_pan(float dx, float dy) {
    // Adjust speed based on zoom (faster when zoomed out)
    viewport.x += dx / viewport.zoom;
    viewport.y += dy / viewport.zoom;
    
    // Update all window positions
    arrange_windows();
}

// Key binding handler
void pan_handler(const Arg *arg) {
    float *delta = (float *)arg->v;  // {dx, dy}
    viewport_pan(delta[0], delta[1]);
}
```

**See**: [[zoom-features#Niri]] for smooth panning ideas

### 2.4 Zoom Implementation

```c
void viewport_set_zoom(float new_zoom) {
    // Clamp zoom range
    if (new_zoom < 0.1f) new_zoom = 0.1f;
    if (new_zoom > 5.0f) new_zoom = 5.0f;
    
    viewport.zoom = new_zoom;
    arrange_windows();
}

void viewport_zoom(float factor) {
    viewport_set_zoom(viewport.zoom * factor);
}

// Key binding handlers
void zoom_in(const Arg *arg) {
    (void)arg;
    viewport_zoom(1.1f);  // 10% zoom in
}

void zoom_out(const Arg *arg) {
    (void)arg;
    viewport_zoom(0.9f);  // 10% zoom out
}

void zoom_reset(const Arg *arg) {
    (void)arg;
    viewport.zoom = 1.0f;
    viewport.x = viewport.y = 0;
    arrange_windows();
}
```

### 2.5 Window Positioning

```c
struct window {
    struct wlr_scene_tree *scene_tree;
    float world_x, world_y;  // Persistent world coordinates
    bool world_set;
    // ... other fields
};

void arrange_windows(void) {
    struct window *win;
    wl_list_for_each(win, &windows, link) {
        if (!win->world_set) continue;
        
        // Calculate screen position
        int screen_x = world_to_screen_x(win->world_x);
        int screen_y = world_to_screen_y(win->world_y);
        
        // Update scene node position
        wlr_scene_node_set_position(&win->scene_tree->node, screen_x, screen_y);
        
        // Scale the window content
        // Note: This scales the buffer, not just the position
        struct wlr_scene_buffer *buf = get_window_buffer(win);
        if (buf) {
            // Original size
            int orig_w = win->width;
            int orig_h = win->height;
            
            // Scaled size
            int scaled_w = (int)(orig_w * viewport.zoom);
            int scaled_h = (int)(orig_h * viewport.zoom);
            
            wlr_scene_buffer_set_dest_size(buf, scaled_w, scaled_h);
        }
    }
}
```

**Important**: This basic approach scales buffers, which causes quality loss. Phase 3 improves this.

**See**: [[scene-graph-scaling#Example Zoom Implementation]]

---

## Phase 3: Quality Improvements

### 3.1 Smart Filter Selection

```c
bool is_integer_scale(float scale) {
    float rounded = roundf(scale);
    return fabsf(scale - rounded) < 0.01f;
}

void set_quality_filter(struct wlr_scene_buffer *buffer, float scale) {
    enum wlr_scale_filter_mode mode;
    
    if (is_integer_scale(scale)) {
        // Integer scale: use nearest for pixel-perfect
        mode = WLR_SCALE_FILTER_NEAREST;
    } else {
        // Fractional scale: use bilinear for smoothness
        mode = WLR_SCALE_FILTER_BILINEAR;
    }
    
    wlr_scene_buffer_set_filter_mode(buffer, mode);
}
```

**See**: [[scaling-algorithms#Implementation Recommendations]], [[scene-graph-scaling#Scaling Quality and Filter Modes]]

### 3.2 Handle Fractional Scales Correctly

```c
void notify_surface_scale(struct wlr_surface *surface, float output_scale) {
    // Send fractional scale (v120 encoded)
    wlr_fractional_scale_v1_notify_scale(surface, output_scale);
    
    // Also send integer buffer scale for legacy clients
    int buffer_scale = (int)ceilf(output_scale);
    wlr_surface_set_preferred_buffer_scale(surface, buffer_scale);
}
```

**See**: [[fractional-scaling#Best Practices]]

### 3.3 Damage Tracking Considerations

```c
void arrange_windows_optimized(void) {
    static float last_zoom = 1.0;
    
    if (viewport.zoom != last_zoom) {
        // Zoom changed: damage entire screen
        damage_all_outputs();
        last_zoom = viewport.zoom;
    }
    
    // ... rest of arrange
}
```

**Note**: wlroots scene graph handles most damage tracking automatically.

**See**: [[scene-graph-scaling#Performance Optimizations]]

---

## Phase 4: Resolution Independence (Advanced)

### 4.1 High-Resolution Rendering

Instead of scaling buffers (which loses quality), render at higher resolution:

```c
// Allocate larger framebuffer
struct wlr_allocator *high_res_alloc;
struct wlr_renderer *renderer;

void setup_high_res_rendering(void) {
    // Create allocator for larger buffers
    high_res_alloc = wlr_allocator_autocreate(backend, renderer);
}

// Render window at 2x resolution
void render_at_high_res(struct window *win, float super_scale) {
    int high_w = (int)(win->width * super_scale);
    int high_h = (int)(win->height * super_scale);
    
    // Allocate high-res buffer
    struct wlr_buffer *high_res_buf = allocate_buffer(high_w, high_h);
    
    // Render at high resolution
    render_window_to_buffer(win, high_res_buf);
    
    // Use as source, scale down for display
    struct wlr_scene_buffer *buf = win->scene_buffer;
    wlr_scene_buffer_set_buffer(buf, high_res_buf);
    
    // Display at normal size (high-res source, filtered down)
    int display_w = (int)(win->width * viewport.zoom);
    int display_h = (int)(win->height * viewport.zoom);
    wlr_scene_buffer_set_dest_size(buf, display_w, display_h);
}
```

**See**: [[buffer-management#Higher Resolution Rendering]]

### 4.2 Buffer Crop & Scale

For cutout regions without reallocating:

```c
void show_cutout(struct window *win, struct wlr_box *region) {
    struct wlr_scene_buffer *buf = win->scene_buffer;
    
    // Crop source to region
    struct wlr_fbox src_box = {
        .x = region->x,
        .y = region->y,
        .width = region->width,
        .height = region->height
    };
    wlr_scene_buffer_set_source_box(buf, &src_box);
    
    // Scale to desired display size
    wlr_scene_buffer_set_dest_size(buf, region->width, region->height);
}
```

**See**: [[buffer-management#Buffer Cropping and Scaling]]

### 4.3 Future: Custom Shaders

For FSR or Lanczos quality, eventually need custom renderer:

```c
// Pseudocode - requires custom wlr_renderer
void render_with_fsr(struct wlr_texture *source, struct wlr_box *output) {
    // Pass 1: Edge-adaptive upsampling (EASU)
    bind_shader(easu_shader);
    set_uniform("source", source);
    set_uniform("output_size", output->width, output->height);
    draw_quad();
    
    // Pass 2: Contrast-adaptive sharpening (RCAS)
    bind_shader(rcas_shader);
    set_uniform("source", intermediate_texture);
    draw_quad();
}
```

**See**: [[scaling-algorithms#Sharp Upscaling Algorithms]], [[zoom-features#Wayfire]]

---

## Testing Checklist

### Basic Functionality
- [ ] Windows open at correct world positions
- [ ] Pan moves camera (not windows)
- [ ] Zoom changes scale
- [ ] Reset returns to 1.0x

### Quality
- [ ] Integer scales (2x, 3x) are pixel-perfect
- [ ] Fractional scales (1.5x) are smooth
- [ ] Text remains readable at all zoom levels
- [ ] No jagged edges on window borders

### Performance
- [ ] 60fps maintained while panning
- [ ] 60fps maintained while zooming
- [ ] No stuttering with multiple windows
- [ ] Damage tracking works (only changed areas redraw)

### Edge Cases
- [ ] Very small zoom (0.1x) works
- [ ] Very large zoom (5.0x) works
- [ ] Windows off-screen don't cause issues
- [ ] Focus changes work while zoomed

---

## Common Pitfalls

### Blurry Integer Scales

**Problem**: 2x zoom looks blurry
**Cause**: Using bilinear filter on integer scale
**Fix**: Use nearest neighbor for integer scales

```c
if (is_integer_scale(viewport.zoom)) {
    wlr_scene_buffer_set_filter_mode(buf, WLR_SCALE_FILTER_NEAREST);
}
```

**See**: [[fractional-scaling#Best Practices]]

### Coordinate Confusion

**Problem**: Windows in wrong position after zoom
**Cause**: Mixing world and screen coordinates
**Fix**: Always transform world→screen at final step

**See**: [[scene-graph-scaling#Coordinate Transformations]]

### Performance Degradation

**Problem**: FPS drops when zoomed out
**Cause**: Rendering off-screen windows
**Fix**: wlroots scene graph handles this automatically

**See**: [[scene-graph-scaling#Performance Optimizations]]

---

## Build Instructions

### Using Nix (Recommended)

```bash
cd ~/kalin-wm
nix develop --command make clean && make
```

### Manual Build

Requires:
- wlroots-0.20 development files
- wayland-server, xkbcommon, libinput
- pkg-config

```bash
make clean && make
```

### Testing

**Nested Mode** (runs inside current Wayland session):
```bash
./dwl -s "foot"
```

**TTY Mode** (requires switching to TTY with Ctrl+Alt+F3):
```bash
./scripts/run-tty
```

---

## Related Resources

- [[api-reference]] - Quick API lookup
- [[wayland-scaling-glossary]] - Term definitions
- [[index]] - Full research vault
- [[fractional-scaling]] - Detailed fractional scale docs
- [[scene-graph-scaling]] - Scene graph API docs
- [[scaling-algorithms]] - Algorithm deep-dive
- [[meta/implementation-notes]] - Actual implementation details for kalin-wm
