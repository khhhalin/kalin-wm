# Wayland Scaling & Zoom Glossary

> Comprehensive definitions of terms, APIs, and concepts from the kalin-wm research vault.

---

## A

### AMD FidelityFX Super Resolution (FSR)
**Category**: Algorithm / GPU

Two-pass upscaling algorithm developed by AMD. Uses Edge-Adaptive Spatial Upsampling (EASU) followed by Robust Contrast-Adaptive Sharpening (RCAS). Provides high-quality upscaling with ~0.8ms GPU time at 4K.

**See Also**: [[scaling-algorithms#AMD FidelityFX Super Resolution 1.0 (FSR)|FSR Details]], [[definitions#Lanczos Resampling|Lanczos]], [[definitions#NIS|NIS]]

---

### Anisotropic Filtering
**Category**: Texture Filtering

Advanced texture filtering that samples more texels at oblique viewing angles. Up to 128 fetches per sample. Best for 3D textures viewed at angles, rarely needed for 2D window compositing.

**See Also**: [[scaling-algorithms#Texture Filtering Modes]], [[definitions#Bilinear Filtering|Bilinear]]

---

## B

### Bilinear Filtering
**Category**: Texture Filtering

Interpolates between 4 neighboring texels using weighted average. Standard GPU hardware implementation. Good balance of quality and performance (4 fetches, ~0.1ms).

```c
// wlroots API
enum wlr_scale_filter_mode filter = WLR_SCALE_FILTER_BILINEAR;
wlr_scene_buffer_set_filter_mode(buffer, filter);
```

**See Also**: [[scaling-algorithms#Bilinear Filtering]], [[definitions#Nearest Neighbor|Nearest]], [[definitions#Lanczos Resampling|Lanczos]]

---

### Buffer Scale
**Category**: Protocol

Integer scale factor (typically ceil of fractional scale) communicated to clients via `wl_surface.set_buffer_scale`. Legacy protocol, less precise than fractional scale.

**See Also**: [[fractional-scaling#Buffer Scale vs Output Scale]], [[definitions#Fractional Scale|Fractional Scale]], [[definitions#Output Scale|Output Scale]]

---

### Buffer Transform
**Category**: Protocol

Rotation and flip transform applied to buffer contents. Used for mobile devices and portrait displays. Values: normal, 90°, 180°, 270°, plus flips.

**See Also**: [[scene-graph-scaling#wlr_scene_buffer]], [[definitions#wlr_scene_buffer|wlr_scene_buffer]]

---

## D

### DMA-BUF
**Category**: Linux / Memory

Linux Direct Memory Access Buffer mechanism for zero-copy buffer sharing between GPU, display controller, and processes. Essential for efficient compositor buffer management.

**See Also**: [[buffer-management#Linux DMA-BUF and Zero-Copy Buffer Sharing]], [[definitions#Zero-Copy|Zero-Copy]]

---

### Damage Tracking
**Category**: Optimization

wlroots automatically tracks which regions of the screen need redrawing. Reduces GPU work by only re-rendering changed areas.

**See Also**: [[scene-graph-scaling#Performance Optimizations]], [[definitions#wlr_scene|wlr_scene]]

---

## F

### FSR
**Category**: Algorithm

See [[definitions#AMD FidelityFX Super Resolution (FSR)|AMD FidelityFX Super Resolution]]

---

### Fractional Scale
**Category**: Protocol

Non-integer scale factor (e.g., 1.5x, 1.25x) communicated via `wp_fractional_scale_v1`. Uses v120 encoding (scale × 120). Enables crisp rendering on high-DPI displays.

```c
// v120 encoding example
// Scale 1.5 = 180 (1.5 * 120)
// Scale 1.25 = 150 (1.25 * 120)
```

**See Also**: [[fractional-scaling#wp_fractional_scale_v1 Protocol]], [[definitions#Buffer Scale|Buffer Scale]]

---

### Fractional Scaling
**Category**: Technique

Rendering content at non-integer scale factors. Requires client-side cooperation and careful handling of edge pixels to avoid blur.

**See Also**: [[fractional-scaling]], [[definitions#Fractional Scale|Fractional Scale]]

---

## I

### Integer Scaling
**Category**: Technique

Scaling by whole numbers (2x, 3x, etc.) using nearest-neighbor filtering. Pixel-perfect but limited to specific ratios. Best for retro content or when exact pixel alignment matters.

**See Also**: [[scaling-algorithms#Integer vs Fractional Scaling]], [[definitions#Nearest Neighbor|Nearest Neighbor]]

---

## L

### Lanczos Resampling
**Category**: Algorithm

High-quality resampling using sinc-sinc kernel. Can be implemented as separable 2-pass for O(n) complexity. Best quality but computationally expensive (~2.5ms at 4K).

**See Also**: [[scaling-algorithms#Lanczos Resampling]], [[definitions#FSR|FSR]], [[definitions#Spline|Spline]]

---

## N

### Nearest Neighbor
**Category**: Texture Filtering

Simplest filtering - selects closest texel. 1 fetch, fastest performance. Pixel-perfect for integer scales but creates aliasing at fractional scales.

```c
enum wlr_scale_filter_mode filter = WLR_SCALE_FILTER_NEAREST;
```

**See Also**: [[scaling-algorithms#Nearest Neighbor]], [[definitions#Integer Scaling|Integer Scaling]]

---

### NIS
**Category**: Algorithm

NVIDIA Image Scaling - single-pass 6-tap filter with sharpening. Faster than FSR but less edge-adaptive. Good middle-ground solution.

**See Also**: [[scaling-algorithms#NVIDIA Image Scaling (NIS)]], [[definitions#FSR|FSR]]

---

## O

### Output Scale
**Category**: Configuration

Float scale factor configured by user for each output (e.g., 1.5 for 150% scaling). Compositor converts this to buffer scale and fractional scale for clients.

**See Also**: [[fractional-scaling#Buffer Scale vs Output Scale]], [[definitions#Fractional Scale|Fractional Scale]]

---

## S

### Scene Graph
**Category**: Architecture

Hierarchical structure for organizing renderable elements. wlroots scene graph provides automatic damage tracking, visibility culling, and protocol integration.

**See Also**: [[scene-graph-scaling]], [[definitions#wlr_scene|wlr_scene]]

---

### Spline
**Category**: Algorithm

Cubic approximation of Lanczos kernel. Variants: Spline16, Spline36, Spline64. Good balance of quality and performance.

**See Also**: [[scaling-algorithms#Spline Interpolation]], [[definitions#Lanczos Resampling|Lanczos]]

---

## T

### Texel
**Category**: Graphics

A texture pixel - the fundamental unit of texture data in GPU memory.

**See Also**: [[scaling-algorithms#Texture Filtering Modes]]

---

### Texture Filtering
**Category**: GPU

Methods for sampling texture data when texture and screen pixels don't align. Modes: nearest, bilinear, trilinear, anisotropic.

**See Also**: [[scaling-algorithms#Texture Filtering Modes]], [[definitions#Bilinear Filtering|Bilinear]], [[definitions#Nearest Neighbor|Nearest]]

---

### Transform Stack
**Category**: Architecture

Layered transformation system for composable effects (position, scale, rotate). Used in advanced compositors like Wayfire.

**See Also**: [[zoom-features#Wayfire View Transformers]], [[scene-graph-scaling#Nested Scene Trees for Zoom]]

---

## V

### v120 Encoding
**Category**: Protocol

Fixed-point encoding for fractional scales where value = scale × 120. Allows precise fractional representation without floating-point in protocol.

```c
// Example conversions
1.5 × 120 = 180
1.25 × 120 = 150
2.0 × 120 = 240
```

**See Also**: [[fractional-scaling#wp_fractional_scale_v1 Protocol]]

---

### Viewport Transform
**Category**: Technique

Transforming coordinates between world, surface, and screen spaces. Essential for implementing zoom and pan functionality.

**See Also**: [[scene-graph-scaling#Coordinate Transformations]], [[zoom-features]]

---

## W

### Wayland
**Category**: Protocol

Display server protocol for Linux. Replaces X11 with modern, secure, efficient architecture. Core protocol plus extensions like fractional-scale-v1, viewporter.

**See Also**: All documents in this vault

---

### wlroots
**Category**: Library

Modular Wayland compositor library. Provides implementations of Wayland protocols, rendering, input handling, and more.

**See Also**: [[fractional-scaling]], [[scene-graph-scaling]], [[buffer-management]]

---

### wlr_buffer
**Category**: API

Buffer abstraction in wlroots. Wraps DMA-BUF, shared memory, or GPU buffers with reference counting and locking.

```c
struct wlr_buffer *buffer = ...;
wlr_buffer_lock(buffer);  // Increment ref count
wlr_buffer_unlock(buffer);  // Decrement ref count
```

**See Also**: [[buffer-management#wlr_buffer and wlr_texture Relationship]]

---

### wlr_fractional_scale_manager_v1
**Category**: API

Global factory for fractional scale protocol objects. Create once at compositor startup.

```c
struct wlr_fractional_scale_manager_v1 *manager =
    wlr_fractional_scale_manager_v1_create(display, 1);
```

**See Also**: [[fractional-scaling#wlroots API]]

---

### wlr_scene
**Category**: API

Root scene graph node containing the main tree and output list. Entry point for scene graph operations.

```c
struct wlr_scene *scene = wlr_scene_create();
```

**See Also**: [[scene-graph-scaling#Scene and Scene Tree]], [[definitions#Scene Graph|Scene Graph]]

---

### wlr_scene_buffer
**Category**: API

Scene graph node displaying a buffer with configurable scaling, cropping, and transforms. Key for zoom implementation.

```c
// Key functions
wlr_scene_buffer_set_dest_size(buffer, width, height);
wlr_scene_buffer_set_source_box(buffer, &crop_box);
wlr_scene_buffer_set_filter_mode(buffer, WLR_SCALE_FILTER_BILINEAR);
```

**See Also**: [[scene-graph-scaling#Scene Buffers and Scaling]]

---

### wlr_scene_tree
**Category**: API

Sub-tree in scene graph for grouping nodes hierarchically. Enables transform stacks and organized scene management.

```c
struct wlr_scene_tree *tree = wlr_scene_tree_create(&scene->tree);
struct wlr_scene_tree *nested = wlr_scene_tree_create(tree);
```

**See Also**: [[scene-graph-scaling#Nested Scene Trees for Zoom]]

---

### wlr_scale_filter_mode
**Category**: API

Enum defining texture filtering quality: `WLR_SCALE_FILTER_BILINEAR` or `WLR_SCALE_FILTER_NEAREST`.

**See Also**: [[scene-graph-scaling#Scaling Quality and Filter Modes]], [[definitions#Bilinear Filtering|Bilinear]], [[definitions#Nearest Neighbor|Nearest]]

---

### wlr_surface
**Category**: API

Wayland surface abstraction in wlroots. Represents client content that can be displayed, scaled, and transformed.

**See Also**: [[fractional-scaling]], [[scene-graph-scaling#wlr_scene_surface]]

---

### wp_fractional_scale_v1
**Category**: Protocol

Wayland protocol extension allowing compositors to communicate preferred fractional scale factors to clients.

**See Also**: [[fractional-scaling#wp_fractional_scale_v1 Protocol]], [[definitions#Fractional Scale|Fractional Scale]]

---

### wp_linux_dmabuf
**Category**: Protocol

Wayland protocol for zero-copy buffer sharing using Linux DMA-BUF. Essential for GPU buffer import.

**See Also**: [[buffer-management#wp_linux_dmabuf Protocol]]

---

### wp_viewporter
**Category**: Protocol

Wayland protocol for viewport cropping and scaling. Clients use source rectangle + destination size for flexible scaling.

**See Also**: [[buffer-management#Buffer Cropping and Scaling]], [[fractional-scaling]]

---

## Z

### Zero-Copy
**Category**: Technique

Buffer sharing mechanism avoiding memory copies between GPU, CPU, and display controller. Uses DMA-BUF handles.

**See Also**: [[buffer-management#Linux DMA-BUF and Zero-Copy Buffer Sharing]], [[definitions#DMA-BUF|DMA-BUF]]

---

## Related Topics

- [[fractional-scaling|Fractional Scaling in wlroots]]
- [[scene-graph-scaling|Scene Graph API]]
- [[zoom-features|Compositor Zoom Features]]
- [[scaling-algorithms|GPU Scaling Algorithms]]
- [[buffer-management|Buffer Management]]
- [[meta/index|Research Index]]
- [[meta/api-reference|API Quick Reference]]
