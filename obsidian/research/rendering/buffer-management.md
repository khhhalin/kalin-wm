# Buffer Management and Resolution Independence in Wayland Compositors

**Key Terms:** [[definitions#DMA-BUF|DMA-BUF]] | [[definitions#Zero-Copy|Zero-Copy]] | [[definitions#wlr_buffer|wlr_buffer]] | [[definitions#wlr_texture|wlr_texture]] | [[definitions#wp_linux_dmabuf|wp_linux_dmabuf]] | [[definitions#Buffer Scale|Buffer Scale]] | [[definitions#Fractional Scaling|Fractional Scaling]]

## Overview

This document explores the architecture and implementation of [[definitions#wlr_buffer|buffer management]] in [[definitions#Wayland|Wayland compositors]], with a focus on [[definitions#Resolution Independence|resolution independence]], [[definitions#HiDPI|high-DPI support]], and [[definitions#Zero-Copy|zero-copy]] buffer sharing via [[definitions#DMA-BUF|Linux DMA-BUF]].

## Table of Contents

1. [[#Linux DMA-BUF and Zero-Copy Buffer Sharing]]
2. [[#wp_linux_dmabuf Protocol]]
3. [[#Buffer vs Surface vs Output Sizes]]
4. [[#Higher Resolution Rendering]]
5. [[#wlr_buffer and wlr_texture]]
6. [[#Buffer Cropping and Scaling]]
7. [[#Implementation Examples]]
8. [[#Best Practices]]

---

## [[definitions#DMA-BUF|Linux DMA-BUF]] and [[definitions#Zero-Copy|Zero-Copy]] Buffer Sharing

### What is [[definitions#DMA-BUF|DMA-BUF]]?

[[definitions#DMA-BUF|DMA-BUF]] (Direct Memory Access Buffer) is a Linux kernel subsystem that allows sharing [[definitions#wlr_buffer|buffers]] between multiple devices and subsystems without copying data through CPU memory. In the context of [[definitions#Wayland|Wayland compositors]], it enables:

- **[[definitions#Zero-Copy|Zero-copy]] buffer sharing**: Clients render directly into GPU-accessible memory
- **[[definitions#Direct Scanout|Direct scanout]]**: Compositor can display buffers without GPU composition
- **Cross-device sharing**: Buffers can move between GPU, display controller, and capture devices

### Architecture

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│   Application   │────->│   GPU (Render)   │────->│  DMA-BUF (fd)   │
│  (OpenGL/Vulkan)│     │                  │     │                 │
└─────────────────┘     └──────────────────┘     └────────┬────────┘
                                                         │
                              ┌─────────────────────────┘
                              │
                              ▼
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│   Compositor    │<-────│  GPU (Import)    │<-────│  DMA-BUF (fd)   │
│   (wlroots)     │     │  (EGLImage/Vk    │     │  (same memory)  │
│                 │     │   Image)         │     │                 │
└────────┬────────┘     └──────────────────┘     └─────────────────┘
         │
         │ Direct Scanout (possible)
         ▼
┌─────────────────────────────────────────────────────────────────┐
│                     Display Controller (DRM/KMS)                  │
│                    (DMA from same physical memory)                │
└─────────────────────────────────────────────────────────────────┘
```

### Buffer Capabilities

wlroots defines three buffer capability types:

```c
enum wlr_buffer_cap {
    WLR_BUFFER_CAP_DATA_PTR = 1 << 0,  // CPU-accessible memory
    WLR_BUFFER_CAP_DMABUF   = 1 << 1,  // DMA-BUF (GPU memory)
    WLR_BUFFER_CAP_SHM      = 1 << 2,  // Shared memory (mmap)
};
```

### [[definitions#DMA-BUF|DMA-BUF]] Attributes Structure

```c
#define WLR_DMABUF_MAX_PLANES 4

struct [[definitions#wlr_dmabuf_attributes|wlr_dmabuf_attributes]] {
    int32_t width, height;
    uint32_t format;        // FourCC code (e.g., DRM_FORMAT_XRGB8888)
    uint64_t [[definitions#Modifier|modifier]];      // Layout modifier (e.g., DRM_FORMAT_MOD_LINEAR)
    
    int n_planes;
    uint32_t offset[WLR_DMABUF_MAX_PLANES];
    uint32_t stride[WLR_DMABUF_MAX_PLANES];
    int fd[WLR_DMABUF_MAX_PLANES];  // File descriptors for each plane
};
```

### Zero-Copy Flow

```
┌─────────────┐         ┌─────────────┐         ┌─────────────┐
│   Client    │         │  Compositor │         │    DRM      │
└──────┬──────┘         └──────┬──────┘         └──────┬──────┘
       │                       │                       │
       │ 1. Create GBM bo      │                       │
       │    (GPU memory)       │                       │
       │                       │                       │
       │ 2. Export FD          │                       │
       │──────────────────────->│                       │
       │                       │                       │
       │                       │ 3. Import as texture  │
       │                       │    (EGLImage/VkImage) │
       │                       │                       │
       │                       │ 4. Can composite      │
       │                       │    OR                 │
       │                       │    5. Direct scanout ─┼───────->
       │                       │                       │
       │                       │                       │
```

---

## [[definitions#wp_linux_dmabuf|wp_linux_dmabuf]] Protocol

### Protocol Overview

The `[[definitions#wp_linux_dmabuf|wp_linux_dmabuf]]` protocol is the standard [[definitions#Wayland|Wayland]] protocol for sharing [[definitions#DMA-BUF|DMA-BUFs]] between clients and compositors. It consists of:

1. **`wp_linux_dmabuf_v1`**: Global factory for creating DMA-BUF based wl_buffers
2. **`wp_linux_buffer_params_v1`**: Temporary object to collect plane information
3. **`wp_linux_dmabuf_feedback_v1`**: Hints for optimal buffer allocation

### Protocol Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│                    Client-Side DMA-BUF Creation                      │
└─────────────────────────────────────────────────────────────────────┘

  Client                              Compositor
    │                                     │
    │ 1. Bind to zwp_linux_dmabuf_v1      │
    │────────────────────────────────────->│
    │                                     │
    │ 2. Receive supported formats        │
    │<-────────────────────────────────────│
    │   (format, modifier pairs)          │
    │                                     │
    │ 3. Create params object             │
    │────────────────────────────────────->│
    │   zwp_linux_dmabuf_v1.create_params │
    │                                     │
    │ 4. Add plane information            │
    │────────────────────────────────────->│
    │   params.add(fd, plane_idx, offset, │
    │             stride, modifier_hi,    │
    │             modifier_lo)            │
    │                                     │
    │ 5. Create buffer                    │
    │────────────────────────────────────->│
    │   params.create_immed(width, height,│
    │                      format, flags) │
    │                                     │
    │ 6. Receive wl_buffer                │
    │<-────────────────────────────────────│
    │                                     │
    │ 7. Attach to surface                │
    │────────────────────────────────────->│
    │   wl_surface.attach(buffer)         │
    │   wl_surface.commit()               │
```

### [[definitions#DMA-BUF|DMA-BUF]] Feedback (v4+)

Feedback helps clients allocate buffers that can be efficiently used by the compositor ([[definitions#wlr_linux_dmabuf_feedback_v1|wlr_linux_dmabuf_feedback_v1]]):

```c
struct wlr_linux_dmabuf_feedback_v1_tranche {
    dev_t target_device;              // Preferred DRM device
    uint32_t flags;
    const struct wlr_drm_format_set *formats;  // Supported format+modifier pairs
};

struct wlr_linux_dmabuf_feedback_v1 {
    dev_t main_device;                // Primary device for allocation
    struct wlr_linux_dmabuf_feedback_v1_tranche *tranches;
    size_t n_tranches;
};
```

### Tranche Preference Order

```
Priority 1: [[definitions#Direct Scanout|Direct scan-out]] to target device
           (format/[[definitions#Modifier|modifier]] supported by display controller)
           
Priority 2: [[definitions#Direct Scanout|Direct scan-out]] possible via compositor
           (format/[[definitions#Modifier|modifier]] supported by GPU texture import)
           
Priority 3: Software fallback (linear layout)
```

---

## [[definitions#wlr_buffer|Buffer]] vs [[definitions#wl_surface|Surface]] vs [[definitions#wlr_output|Output]] Sizes

### Size Relationships

```
┌─────────────────────────────────────────────────────────────────┐
│  BUFFER (pixel coordinates)                                     │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                                                         │   │
│  │    ┌─────────────────────────┐  src_box (cropping)      │   │
│  │    │                         │                         │   │
│  │    │   SURFACE (logical)     │                         │   │
│  │    │   ┌─────────────────┐   │                         │   │
│  │    │   │                 │   │                         │   │
│  │    │   │   dst_size      │   │                         │   │
│  │    │   │   (scaling)     │   │                         │   │
│  │    │   │                 │   │                         │   │
│  │    │   └─────────────────┘   │                         │   │
│  │    │                         │                         │   │
│  │    └─────────────────────────┘                         │   │
│  │                                                         │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ Output scale applied
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  OUTPUT (physical pixels)                                       │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  Transformed & Scaled to output resolution              │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

### Coordinate Transformation Pipeline

```
Buffer Coordinates (pixels)
    │
    ├──-> buffer_transform (rotation/flip)
    │
    ├──-> buffer_scale (integer scaling)
    │
    └──-> wp_viewport crop & scale (fractional)
             │
             ▼
    Surface Coordinates (logical)
             │
             ├──-> surface position (x, y)
             │
             └──-> output scale
                      │
                      ▼
    Output Coordinates (physical pixels)
```

### Size Constraints

| Aspect | Description | Example |
|--------|-------------|---------|
| Buffer Size | Actual pixel dimensions of the attached wl_buffer | 3840×2160 |
| Surface Size | Logical size after viewport transformation | 1920×1080 |
| Output Size | Physical display resolution | 2560×1440 |

**Important**: For [[definitions#Integer Scaling|integer scaling]], [[definitions#wlr_buffer|buffer]] size must be divisible by scale:
```
if (buffer_width % scale != 0 || buffer_height % scale != 0) {
    // Protocol error!
}
```

---

## Higher Resolution Rendering

### Use Cases

1. **Super-sampling (SSAA)**: Render at higher resolution, downsample for anti-aliasing
2. **Zoom functionality**: Magnify portion of screen without quality loss ([[definitions#Resolution Independence|resolution independence]])
3. **HDR compositing**: Work in higher bit-depth intermediate [[definitions#wlr_buffer|buffers]]
4. **Performance scaling**: Render games at lower resolution, [[definitions#FSR|upscale]]

### Gamescope's Approach

Gamescope (Valve's nested compositor) demonstrates advanced resolution handling:

```
┌─────────────────────────────────────────────────────────────────┐
│                     Gamescope Architecture                       │
└─────────────────────────────────────────────────────────────────┘

  Game/App
     │
     │ Renders at "internal" resolution (-w 1280 -h 720)
     │ (e.g., 1280×720 for performance)
     ▼
┌─────────────────┐
│  Input Buffer   │<-── DMA-BUF from client
│  (1280×720)     │
└────────┬────────┘
         │
         │ Vulkan Compute/Upscaling
         │ (FSR, NIS, Integer, Linear)
         ▼
┌─────────────────┐
│ Output Buffer   │──-> "output" resolution (-W 1920 -H 1080)
│ (1920×1080)     │    (matches display or target)
└────────┬────────┘
         │
         ▼
    Parent Compositor (nested mode)
         │
         ▼
    Display
```

### Resolution Decoupling in [[definitions#wlroots|wlroots]]

The key is decoupling [[definitions#wlr_buffer|buffer]] size from [[definitions#wl_surface|surface]] size using `[[definitions#wp_viewporter|wp_viewporter]]`:

```c
// Client requests surface size different from buffer size
struct wp_viewport *viewport = wp_viewporter_get_viewport(viewporter, surface);

// Buffer is 3840×2160 (4K)
// But surface displays as 1920×1080 (FHD)
wp_viewport_set_source(viewport, 
    wl_fixed_from_int(0),     // src_x
    wl_fixed_from_int(0),     // src_y
    wl_fixed_from_int(3840),  // src_width
    wl_fixed_from_int(2160)   // src_height
);
wp_viewport_set_destination(viewport, 1920, 1080);

wl_surface_attach(surface, buffer_4k, 0, 0);
wl_surface.commit();
```

### Zoom Implementation Pattern

```
┌─────────────────────────────────────────────────────────────────┐
│                    Zoom without Re-allocation                    │
└─────────────────────────────────────────────────────────────────┘

High-res backing buffer (e.g., 7680×4320 - 8K)
┌───────────────────────────────────────────────────────────────┐
│                                                               │
│    ┌─────────┐                                                │
│    │ Zoom    │  src_box changes                               │
│    │ Region  │  based on zoom level                           │
│    │ (2x)    │  and pan position                              │
│    │         │                                                │
│    │ ┌───┐   │                                                │
│    │ │   │   │  Only this region is sampled                   │
│    │ └───┘   │  and scaled to output                          │
│    └─────────┘                                                │
│                                                               │
└───────────────────────────────────────────────────────────────┘
         │
         │ Same buffer, different src_box
         ▼
    Output (4K)
```

---

## [[definitions#wlr_buffer|wlr_buffer]] and [[definitions#wlr_texture|wlr_texture]]

### Relationship Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│              wlr_buffer vs wlr_texture Relationship              │
└─────────────────────────────────────────────────────────────────┘

  wlr_buffer (abstract memory container)
  │
  ├──-> wlr_shm_attributes        (CPU mmap'd memory)
  │
  ├──-> wlr_dmabuf_attributes     (GPU DMA-BUF)
  │      │
  │      │ Import into renderer
  │      ▼
  │   wlr_texture (GPU texture object)
  │   │
  │   ├──-> GLES2: EGLImage + GL texture
  │   │
  │   ├──-> Vulkan: VkImage + VkDeviceMemory
  │   │
  │   └──-> Pixman: pixman_image_t (CPU)
  │
  └──-> Client Buffer (wlr_client_buffer)
         │
         ├──-> Source wlr_buffer (locked)
         │
         └──-> Cached wlr_texture (for rendering)

Key Insight: Multiple [[definitions#wlr_texture|textures]] can reference the same [[definitions#wlr_buffer|buffer]]
(but usually 1:1 in practice)
```

### [[definitions#wlr_buffer|wlr_buffer]] Structure

```c
struct [[definitions#wlr_buffer|wlr_buffer]] {
    const struct wlr_buffer_impl *impl;
    
    int width, height;
    
    // Lifecycle management
    bool dropped;           // Producer done
    size_t n_locks;         // Consumer reference count
    
    struct {
        struct wl_signal destroy;
        struct wl_signal release;  // All consumers done
    } events;
    
    struct wlr_addon_set addons;  // Renderer-specific data
};
```

### [[definitions#wlr_texture|wlr_texture]] Structure

```c
struct [[definitions#wlr_texture|wlr_texture]] {
    const struct wlr_texture_impl *impl;
    uint32_t width, height;
    
    struct [[definitions#wlr_renderer|wlr_renderer]] *renderer;  // Texture is bound to renderer
};
```

### Buffer-to-Texture Conversion

```c
// Import [[definitions#DMA-BUF|DMA-BUF]] directly as [[definitions#wlr_texture|texture]] ([[definitions#Zero-Copy|zero copy]])
struct [[definitions#wlr_texture|wlr_texture]] *texture = wlr_texture_from_dmabuf(renderer, &[[definitions#wlr_dmabuf_attributes|dmabuf_attrs]]);

// Or import from generic [[definitions#wlr_buffer|buffer]]
struct [[definitions#wlr_texture|wlr_texture]] *texture = wlr_texture_from_buffer(renderer, buffer);

// For mutable textures (shm only)
struct wlr_texture *texture = wlr_texture_from_pixels(
    renderer, format, stride, width, height, pixel_data);

// Update existing texture with new buffer content
bool success = wlr_texture_update_from_buffer(texture, new_buffer, &damage_region);
```

### [[definitions#wlr_client_buffer|wlr_client_buffer]] (Client Integration)

```c
struct [[definitions#wlr_client_buffer|wlr_client_buffer]] {
    struct [[definitions#wlr_buffer|wlr_buffer]] base;
    
    // GPU [[definitions#wlr_texture|texture]] for compositing
    struct [[definitions#wlr_texture|wlr_texture]] *texture;
    
    // Original source [[definitions#wlr_buffer|buffer]] (locked until [[definitions#wlr_texture|texture]] released)
    struct [[definitions#wlr_buffer|wlr_buffer]] *source;
};

// Creation flow:
// 1. Client commits wl_buffer
// 2. wlroots creates wlr_buffer from resource
// 3. wlr_client_buffer_create() imports as texture
// 4. Source buffer is locked until texture destroyed
```

### Locking Lifecycle

```
┌─────────────────────────────────────────────────────────────────┐
│                    Buffer Reference Lifecycle                    │
└─────────────────────────────────────────────────────────────────┘

Time ─────────────────────────────────────────────────────────────->

Client          Compositor              Renderer
  │                 │                       │
  │ create buffer   │                       │
  ├────────────────->│                       │
  │                 │ wlr_buffer_lock()     │
  │                 ├──────────────────────->│
  │                 │                       │
  │ attach          │                       │
  ├────────────────->│                       │
  │                 │ wlr_buffer_drop()     │
  │                 ├──────────┐            │
  │                 │          │ (client    │
  │ buffer.destroy  │<-─────────┘  can reuse)
  ├────────────────->│                       │
  │                 │                       │
  │                 │        render         │
  │                 │<-──────────────────────┤
  │                 │                       │
  │                 │ wlr_buffer_unlock()   │
  │                 ├──────────────────────->│
  │                 │                       │
  │                 │<-──────────┐ release   │
  │                 │           │ event     │
  │                 │<-──────────┘ (if last) │
  │                 │                       │
  │                 │ buffer destroyed      │
  │                 │ (when dropped+0 locks)│
```

---

## Buffer Cropping and Scaling

### wp_viewporter Protocol

The viewporter protocol enables decoupling buffer size from surface size:

```
┌─────────────────────────────────────────────────────────────────┐
│                    Viewporter Transformations                    │
└─────────────────────────────────────────────────────────────────┘

Source Rectangle (src_x, src_y, src_width, src_height)
        │
        │ Defines region of buffer to sample
        ▼
┌─────────────────────────────────────┐
│  Source can be:                     │
│  - NULL: use entire buffer          │
│  - Integer size: crop only          │
│  - Fractional size: crop + scale    │
└─────────────────────────────────────┘
        │
        │ Scaling to destination
        ▼
Destination Size (dst_width, dst_height)
        │
        │ Defines surface size in logical coords
        ▼
┌─────────────────────────────────────┐
│  Destination can be:                │
│  - (-1, -1): use source size        │
│  - Specific: scale to this size     │
└─────────────────────────────────────┘
```

### Transformation Order

```c
// Coordinate transformations are applied in this order:
// 1. buffer_transform (wl_surface.set_buffer_transform)
// 2. buffer_scale (wl_surface.set_buffer_scale)  
// 3. crop and scale (wp_viewport.set_*)

// Example: High-DPI display with rotation
wl_surface.set_buffer_transform(WL_OUTPUT_TRANSFORM_90);
wl_surface.set_buffer_scale(2);
wp_viewport.set_source(0, 0, 
    wl_fixed_from_double(1920.5),  // fractional
    wl_fixed_from_double(1080.5));
wp_viewport.set_destination(3840, 2160);
```

### Scene Graph Integration (wlr_scene)

```c
struct wlr_scene_buffer {
    struct wlr_scene_node node;
    struct wlr_buffer *buffer;
    
    // Source rectangle (cropping)
    struct wlr_fbox src_box;
    
    // Destination size (scaling)
    int dst_width, dst_height;
    
    // Rendering state
    struct wlr_texture *texture;
    enum wlr_scale_filter_mode filter_mode;
    float opacity;
    // ...
};

// Set source (crop)
wlr_scene_buffer_set_source_box(scene_buffer, &(struct wlr_fbox){
    .x = 100, .y = 100,
    .width = 800, .height = 600
});

// Set destination (scale)
wlr_scene_buffer_set_dest_size(scene_buffer, 1920, 1080);
```

### Zoom Implementation with [[definitions#wlr_scene|wlr_scene]]

```c
// Zoom into a region of a high-res [[definitions#wlr_buffer|buffer]]
void set_zoom_region(struct [[definitions#wlr_scene_buffer|wlr_scene_buffer]] *buffer,
                     float zoom_level,     // e.g., 2.0 for 2x zoom
                     float center_x,       // Normalized 0-1
                     float center_y) {
    
    int buf_w = buffer->[[definitions#wlr_buffer|buffer]]->width;
    int buf_h = buffer->[[definitions#wlr_buffer|buffer]]->height;
    
    // Calculate source region
    float src_w = buf_w / zoom_level;
    float src_h = buf_h / zoom_level;
    float src_x = center_x * buf_w - src_w / 2;
    float src_y = center_y * buf_h - src_h / 2;
    
    // Clamp to buffer bounds
    src_x = fmax(0, fmin(src_x, buf_w - src_w));
    src_y = fmax(0, fmin(src_y, buf_h - src_h));
    
    // Set source box (what region to sample)
    wlr_scene_buffer_set_source_box(buffer, &(struct wlr_fbox){
        .x = src_x, .y = src_y,
        .width = src_w, .height = src_h
    });
    
    // Set destination (output size - stays constant)
    wlr_scene_buffer_set_dest_size(buffer, output_width, output_height);
}
```

---

## Implementation Examples

### DMA-BUF Import in Vulkan Renderer

```c
// From wlroots render/vulkan/texture.c

struct wlr_texture *vulkan_texture_from_buffer(
        struct wlr_renderer *wlr_renderer, struct wlr_buffer *buffer) {
    struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
    
    // Check if already imported
    struct wlr_addon *addon = wlr_addon_find(&buffer->addons, 
        renderer, &texture_addon_impl);
    if (addon) {
        struct wlr_vk_texture *texture = wl_container_of(addon, texture, buffer_addon);
        return &texture->wlr_texture;
    }
    
    // Try DMA-BUF import
    struct wlr_dmabuf_attributes dmabuf;
    if (!wlr_buffer_get_dmabuf(buffer, &dmabuf)) {
        // Fall back to data ptr access
        return vulkan_texture_from_data_ptr_access(renderer, buffer);
    }
    
    // Import DMA-BUF as VkImage
    VkDeviceMemory mems[WLR_DMABUF_MAX_PLANES];
    uint32_t n_mems;
    VkImage image = vulkan_import_dmabuf(renderer, &dmabuf, mems, &n_mems, 
        false, &using_mutable_srgb);
    
    // Create texture wrapper
    struct wlr_vk_texture *texture = calloc(1, sizeof(*texture));
    texture->buffer = buffer;
    texture->image = image;
    texture->dmabuf_imported = true;
    texture->owned = false;  // Don't own the underlying memory
    
    // Add addon for caching
    wlr_addon_init(&texture->buffer_addon, &buffer->addons, 
        renderer, &texture_addon_impl);
    
    return &texture->wlr_texture;
}
```

### GLES2 DMA-BUF Import

```c
// From wlroots render/gles2/texture.c

static struct wlr_texture *gles2_texture_from_dmabuf(
        struct wlr_renderer *wlr_renderer,
        struct wlr_dmabuf_attributes *attribs) {
    struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);
    
    // Create EGLImage from DMA-BUF
    EGLImageKHR image = eglCreateImageKHR(renderer->egl->display,
        EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL,
        (EGLint[]){
            EGL_WIDTH, attribs->width,
            EGL_HEIGHT, attribs->height,
            EGL_LINUX_DRM_FOURCC_EXT, attribs->format,
            EGL_DMA_BUF_PLANE0_FD_EXT, attribs->fd[0],
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, attribs->offset[0],
            EGL_DMA_BUF_PLANE0_PITCH_EXT, attribs->stride[0],
            EGL_NONE
        });
    
    // Create GL texture and bind EGLImage
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, image);
    
    // Create wrapper
    struct wlr_gles2_texture *texture = calloc(1, sizeof(*texture));
    texture->tex = tex;
    texture->image = image;
    texture->has_alpha = drm_format_has_alpha(attribs->format);
    
    return &texture->wlr_texture;
}
```

### Scene Graph Rendering Pipeline

```c
// Simplified render flow from wlr_scene

void render_scene_output(struct wlr_scene_output *scene_output) {
    struct wlr_output *output = scene_output->output;
    
    // Begin render pass
    struct wlr_render_pass *pass = wlr_output_begin_render_pass(output, 
        &scene_output->damage_ring, &color_transform);
    
    // Collect visible render entries
    struct wl_array render_list;
    scene_collect_render_list(scene_output, &render_list);
    
    // Sort by z-order
    qsort(render_list.data, render_list.size, sizeof(struct render_entry),
        compare_render_entries);
    
    // Render each entry
    struct render_entry *entry;
    wl_array_for_each(entry, &render_list) {
        struct wlr_scene_buffer *buffer = entry->buffer;
        
        if (buffer->buffer) {
            // Import or get cached texture
            struct wlr_texture *texture = scene_buffer_get_texture(buffer);
            if (!texture) continue;
            
            // Set up transform
            struct wlr_render_texture_options opts = {
                .texture = texture,
                .src_box = buffer->src_box,
                .dst_box = { 
                    entry->x, entry->y, 
                    buffer->dst_width, buffer->dst_height 
                },
                .transform = buffer->transform,
                .filter_mode = buffer->filter_mode,
                .opacity = buffer->opacity,
            };
            
            // Add to render pass
            wlr_render_pass_add_texture(pass, &opts);
        }
    }
    
    // Submit
    wlr_render_pass_submit(pass);
    wlr_output_commit(output);
}
```

---

## Best Practices

### High-DPI Support

```
┌─────────────────────────────────────────────────────────────────┐
│                    High-DPI Implementation                       │
└─────────────────────────────────────────────────────────────────┘

1. Use wp_fractional_scale_manager_v1
   - Client receives preferred fractional scale
   - Buffer size = surface_size × scale
   
2. Buffer Size Calculation:
   
   Integer scale (1, 2, 3...):
   ┌─────────────────┐
   │ Surface: 100×50 │
   │ Scale: 2        │
   │ Buffer: 200×100 │
   └─────────────────┘
   
   Fractional scale (1.5):
   ┌─────────────────┐
   │ Surface: 100×50 │
   │ Scale: 1.5      │
   │ Buffer: 150×75  │
   │ Viewport dst:   │
   │   100×50        │
   └─────────────────┘

3. Scale Filter Selection:
   - Linear: Smooth scaling (good for photos)
   - Nearest: Sharp pixels (good for pixel art)
   - FSR/NIS: Upscaling algorithms (games)
```

### Zero-Copy Optimization Checklist

```markdown
- [ ] Client allocates with optimal format/modifier
      (use linux_dmabuf_feedback_v1)
      
- [ ] Compositor imports via EGLImage/VkImage
      (not read_pixels + upload)
      
- [ ] Direct scanout when possible
      (no composition needed)
      
- [ ] Avoid CPU readback
      (except for screenshots/screen capture)
      
- [ ] Reuse textures for same buffer
      (use wlr_texture_from_buffer caching)
      
- [ ] Proper synchronization
      (implicit via DMA-BUF or explicit syncobj)
```

### Multi-GPU Considerations

```
┌─────────────────────────────────────────────────────────────────┐
│                    Multi-GPU Buffer Sharing                      │
└─────────────────────────────────────────────────────────────────┘

GPU 1 (Render)          GPU 2 (Display)
   │                         │
   │ Allocate DMA-BUF         │
   │ with device-local        │
   │ modifier                 │
   │                         │
   │ Render                  │
   │                         │
   │ Export FD               │
   ├────────────────────────->│
   │                         │ Import to EGL/Vk
   │                         │ (may need copy if
   │                         │  modifiers differ)
   │                         │
   │                         │ Display/Composite

Fallback: Copy via OpenGL/Vulkan when direct import fails
```

### Zoom Implementation Best Practices

1. **Pre-allocate large buffers**: Allocate once at max expected resolution
2. **Use src_box for panning**: Adjust source rectangle, not buffer
3. **Cache textures**: Don't re-import buffers every frame
4. **Consider mipmaps**: For large zoom-out factors, use pre-filtered levels
5. **Filter selection**:
   - Zoom in (>1x): Linear filtering
   - Zoom out (<1x): Mipmapped linear or anisotropic

```c
// Example: Efficient zoom implementation
struct zoom_state {
    struct wlr_buffer *backing_buffer;  // Large, allocated once
    struct wlr_texture *texture;         // Cached import
    
    float zoom_level;
    float pan_x, pan_y;
};

void update_zoom(struct zoom_state *state, float new_zoom, 
                 float new_pan_x, float new_pan_y) {
    // Just update parameters - no buffer reallocation
    state->zoom_level = new_zoom;
    state->pan_x = new_pan_x;
    state->pan_y = new_pan_y;
    
    // Calculate source rectangle
    calculate_src_box(state);
}
```

### Common Pitfalls

```markdown
| Pitfall | Solution |
|---------|----------|
| Buffer size not divisible by scale | Round buffer dimensions or use viewporter |
| Modifier mismatch between GPU and display | Use feedback tranches, fallback to copy |
| Synchronization issues | Use explicit sync (linux_drm_syncobj_v1) |
| Memory bloat from large buffers | Implement LRU cache, drop unused textures |
| Y-inverted buffers | Check dmabuf.flags, flip in shader if needed |
| Multi-plane format handling | Iterate all planes, don't assume single plane |
```

---

## References

- [Wayland Protocols - linux-dmabuf](https://wayland.app/protocols/linux-dmabuf-v1)
- [Wayland Protocols - viewporter](https://wayland.app/protocols/viewporter)
- [Wayland Protocols - fractional-scale](https://wayland.app/protocols/fractional-scale-v1)
- [wlroots Documentation](https://wlroots.org/)
- [Gamescope Repository](https://github.com/ValveSoftware/gamescope)
- [Linux Kernel - DMA-BUF](https://www.kernel.org/doc/html/latest/driver-api/dma-buf.html)
