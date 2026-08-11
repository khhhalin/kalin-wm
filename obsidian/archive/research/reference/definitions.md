# Technical Definitions Glossary

A comprehensive glossary of technical terms used throughout the kalin-wm research vault.

---

## Table of Contents

- [A](#a)
- [B](#b)
- [C](#c)
- [D](#d)
- [E](#e)
- [F](#f)
- [G](#g)
- [H](#h)
- [I](#i)
- [J](#j)
- [L](#l)
- [M](#m)
- [N](#n)
- [O](#o)
- [P](#p)
- [R](#r)
- [S](#s)
- [T](#t)
- [V](#v)
- [W](#w)
- [Z](#z)

---

## A

### Anisotropic Filtering

A texture filtering method that samples along the projected pixel's elongated footprint to preserve detail at oblique viewing angles. Uses multiple samples along the anisotropic direction.

**Characteristics:**
- Memory fetches: Variable (up to 128 theoretical per sample)
- Performance: Memory bandwidth intensive
- Quality: Excellent at oblique angles
- Best for: Ground planes, textured surfaces viewed at angles

---

## B

### Bilinear Filtering

A texture filtering method that performs linear interpolation between the 4 nearest texels. Uses the formula: `lerp(lerp(c00, c10, fx), lerp(c01, c11, fx), fy)`.

**Characteristics:**
- Memory fetches: 4 per sample
- Performance: Very fast (hardware accelerated)
- Quality: Smooth, can appear blurry at high magnification
- Best for: General purpose scaling, video, smooth gradients

**See also:** [[#Nearest Neighbor|Nearest Neighbor]], [[#Lanczos Resampling|Lanczos Resampling]]

### Buffer Scale

The integer scale factor applied to a buffer relative to surface coordinates in Wayland. When a client renders at a higher resolution for HiDPI displays, the buffer scale indicates this ratio.

**Example:** A surface with logical size 100×50 and buffer scale 2 uses a 200×100 buffer.

**See also:** [[#Output Scale|Output Scale]], [[#Fractional Scaling|Fractional Scaling]]

### Buffer Transform

The rotation or flip transformation applied to a buffer before display. Used for handling device orientation (tablets, phones) or correcting scanout order.

**Transform values:**
- Normal, 90°, 180°, 270° rotations
- Horizontal flip, vertical flip
- Combined rotate + flip

**See also:** [[#Viewport Transform|Viewport Transform]]

---

## C

### DMA-BUF (Direct Memory Access Buffer)

A Linux kernel subsystem that enables sharing buffers between multiple devices and subsystems without CPU memory copies. In Wayland compositors, DMA-BUF enables zero-copy buffer sharing where clients render directly into GPU-accessible memory.

**Key capabilities:**
- Zero-copy buffer sharing between GPU and display controller
- Direct scanout (display buffers without composition)
- Cross-device sharing between GPU, display controller, capture devices

**See also:** [[#Zero-Copy|Zero-Copy]], [[#wp_linux_dmabuf|wp_linux_dmabuf]]

---

## D

### Damage Tracking

A rendering optimization where only changed (damaged) regions of the screen are redrawn instead of the entire framebuffer. The wlroots scene graph provides automatic damage tracking.

**Benefits:**
- Reduced GPU workload
- Lower power consumption
- Better performance, especially at high resolutions

### Direct Scanout

Displaying a client buffer directly on the screen without GPU composition. Requires buffer format/modifier compatibility with the display controller.

**Requirements:**
- DMA-BUF with supported format/modifier
- No transformations applied
- Fullscreen or unoccluded window

---

## E

### EASU (Edge Adaptive Spatial Upsampling)

The first pass of AMD FSR 1.0, implementing an adaptive elliptical filter with edge detection for direction-aware sampling. Uses a modified Lanczos-based kernel with ringing reduction.

**See also:** [[#FSR|FSR]], [[#RCAS|RCAS]]

### EGLImage

A Khronos standard for sharing GPU textures between APIs/contexts. In wlroots, used to import DMA-BUFs as OpenGL textures via `EGL_ANDROID_image_native_buffer` or `EGL_EXT_image_dma_buf_import`.

### EWA Lanczos (Elliptical Weighted Average)

A high-quality image scaling algorithm using elliptical weighting based on the Jinc function. Provides excellent quality but computationally expensive, suitable for still images rather than real-time rendering.

---

## F

### FidelityFX Super Resolution (FSR)

AMD's open-source spatial upscaling technology. FSR 1.0 is a two-pass filter using modified Lanczos resampling.

**Pass 1 - EASU:** Edge Adaptive Spatial Upsampling with adaptive elliptical filtering
**Pass 2 - RCAS:** Robust Contrast Adaptive Sharpening to restore detail

**Quality modes:** Ultra Quality (1.3x), Quality (1.5x), Balanced (1.7x), Performance (2.0x)

**See also:** [[#EASU|EASU]], [[#RCAS|RCAS]], [[#NIS|NIS]]

### Fractional Scaling

Scaling content by non-integer factors (e.g., 1.25x, 1.5x, 1.75x) required by many modern HiDPI displays. Typically implemented via two-pass rendering: oversample at next integer scale, then downscale.

**Protocol:** `wp_fractional_scale_v1` allows compositors to communicate preferred fractional scales to clients.

**See also:** [[#Integer Scaling|Integer Scaling]], [[#wp_fractional_scale_v1|wp_fractional_scale_v1]]

### Framebuffer Coordinate System (FCS)

The coordinate system used for actual pixel rendering to the output framebuffer. Distinct from logical coordinate systems used for window positioning.

**See also:** [[#Logical Coordinate System|Logical Coordinate System]]

---

## G

### GBM (Generic Buffer Management)

A Mesa library for allocating buffers suitable for use with DRM/KMS. Used by Wayland clients to create DMA-BUFs for zero-copy rendering.

---

## H

### HiDPI (High Dots Per Inch)

Displays with high pixel density requiring scaling for readable UI. Common scales: 1.5x, 2x (Retina), 3x.

**Implementation approaches:**
- Integer scaling (crisp, but large UI)
- Fractional scaling (flexible, may cause blur)
- Resolution-independent rendering (ideal)

---

## I

### Integer Scaling

Scaling content by whole number factors (1x, 2x, 3x, 4x) preserving pixel-perfect sharpness. Used for pixel art, retro games, and when exact pixel alignment is required.

**Algorithm:**
```
scale_factor = floor(target_size / source_size)
output_size = source_size * scale_factor
padding = (target_size - output_size) / 2
```

**See also:** [[#Fractional Scaling|Fractional Scaling]]

---

## J

### Jinc

A Bessel function used as a filter kernel in EWA (Elliptical Weighted Average) filtering. Similar to sinc but uses Bessel functions instead.

---

## L

### Lanczos Resampling

A high-quality image scaling algorithm using a windowed sinc function as its kernel. Provides good sharpness with reduced ringing compared to pure sinc.

**Kernel:**
```
L(x) = sinc(x) * sinc(x/a)  if |x| < a
       0                    otherwise

where a = number of lobes (typically 2 or 3)
```

**Variants:** Lanczos-2 (4 taps), Lanczos-3 (6 taps), EWA Lanczos (variable)

**See also:** [[#Sinc|Sinc]], [[#Spline Interpolation|Spline Interpolation]]

### Layer Shell (zwlr_layer_shell_v1)

A Wayland protocol for creating desktop layers (background, bottom, top, overlay) that exist outside regular window stacking. Used for panels, wallpapers, and OSDs.

**Layers (bottom to top):** background, bottom, top, overlay

### Logical Coordinate System

The abstract coordinate system used for window positioning and sizes, independent of physical pixel density. Applications work in logical coordinates; compositors convert to physical pixels using output scale.

**See also:** [[#Framebuffer Coordinate System|Framebuffer Coordinate System]]

---

## M

### Modifier

A DRM format modifier describes memory layout constraints for a buffer (tiling, compression, etc.). Critical for zero-copy buffer sharing between GPU and display controller.

**Examples:** `DRM_FORMAT_MOD_LINEAR`, vendor-specific tiling formats

### Mipmap

Pre-calculated, optimized sequences of images at different resolutions. Used for trilinear filtering and efficient minification (scaling down).

---

## N

### Nearest Neighbor

The simplest texture filtering method selecting the single closest texel without interpolation. Fastest option, produces sharp pixelated results.

**Characteristics:**
- Memory fetches: 1 per sample
- Performance: Fastest
- Quality: Sharp edges, aliasing artifacts
- Best for: Pixel art, retro games, integer scaling

**See also:** [[#Bilinear Filtering|Bilinear Filtering]]

### NVIDIA Image Scaling (NIS)

NVIDIA's spatial upscaling algorithm using a 6-tap adaptive filter with sharpening. Simpler than FSR (single pass vs two pass) but effective.

**Key differences from FSR:**
- Single pass vs FSR's two passes
- Less edge adaptation but faster
- Works on all GPUs (not just NVIDIA)

**See also:** [[#FSR|FSR]]

---

## O

### Output Scale

The scale factor applied at the output/display level, converting logical coordinates to physical pixels. May differ from buffer scale in fractional scaling scenarios.

**See also:** [[#Buffer Scale|Buffer Scale]], [[#Fractional Scaling|Fractional Scaling]]

---

## P

### Pixman

A low-level software library for pixel manipulation. Used by wlroots as a CPU rendering fallback and for region operations (damage tracking).

### Protocol (Wayland)

The fundamental unit of communication in Wayland. Defines objects, requests, and events exchanged between clients and compositors. Protocol extensions add functionality.

---

## R

### RCAS (Robust Contrast Adaptive Sharpening)

The second pass of AMD FSR 1.0, applying contrast-adaptive sharpening to remove softening introduced by upscaling.

**See also:** [[#FSR|FSR]], [[#EASU|EASU]]

### Resolution Independence

Rendering content at the target resolution rather than scaling lower-resolution buffers. Key to crisp, blur-free zoom and fractional scaling.

**Approach:**
- Calculate required resolution based on zoom level
- Render viewports at appropriate resolution
- Apply transforms via transformation matrices

**Benefits:** No quality loss, crisp text at all scales

---

## S

### Scene Graph

A tree data structure organizing graphical elements hierarchically. wlroots provides `wlr_scene` for declarative rendering with automatic damage tracking and visibility culling.

**Key features:**
- Hierarchical transforms (position, scale)
- Automatic damage tracking
- Visibility culling (skip off-screen content)
- Opaque region optimization

**See also:** [[#wlr_scene|wlr_scene]], [[#wlr_scene_buffer|wlr_scene_buffer]]

### Sinc Function

The ideal reconstruction filter: `sinc(x) = sin(πx) / (πx)`. Theoretical basis for Lanczos and other high-quality scaling algorithms.

### Spline Interpolation

Cubic spline approximation of Lanczos with better performance. Variants include Spline16 (2 taps), Spline36 (3 taps), Spline64 (4 taps).

**mpv defaults:** `scale=spline36` for upscale, `dscale=mitchell` for downscale

---

## T

### Taps

The number of input samples used per output pixel in a filter kernel. More taps generally mean higher quality but lower performance.

**Examples:**
- Nearest: 1 tap
- Bilinear: 4 taps
- Lanczos-2: 4 taps per dimension
- Lanczos-3: 6 taps per dimension

### Texel

A texture element - the fundamental unit of a texture, equivalent to a pixel in an image but existing in texture/UV space.

### Trilinear Filtering

Bilinear filtering across two mipmap levels, blending based on Level of Detail (LOD). Smoother transitions than bilinear alone for minification.

**Characteristics:**
- Memory fetches: 8 per sample
- Performance: Fast (hardware)
- Quality: Smooth transitions, no mipmap popping

---

## V

### Viewport Transform

The transformation applied by `wp_viewporter` to map buffer pixels to surface coordinates. Enables cropping and scaling independently of buffer dimensions.

**Components:**
- Source rectangle (crop from buffer)
- Destination size (scale to surface)

**See also:** [[#Buffer Transform|Buffer Transform]], [[#wp_viewporter|wp_viewporter]]

### View Transformer (Wayfire)

Wayfire's pluggable transformation system for applying effects (zoom, blur, rotation) to views through a chain of transform operations.

**See also:** [[#Scene Graph|Scene Graph]]

### Vulkan

A modern, low-overhead graphics API. wlroots includes a Vulkan renderer option with advantages for compute shaders and advanced scaling algorithms.

---

## W

### Wayland

A display server protocol intended as a simpler, more secure replacement for X11. Compositors implement the server side; toolkit applications are clients.

### wl_buffer

Wayland protocol object representing a shared memory or DMA-BUF buffer containing pixel data. The basic unit of surface content.

### wl_surface

The fundamental drawable object in Wayland. Clients attach buffers to surfaces, and compositors display surfaces. Supports transforms, scaling, and input regions.

### wp_fractional_scale_v1

Wayland protocol extension allowing compositors to communicate preferred fractional scales to clients. Clients render at the next integer scale; compositor downscales.

**See also:** [[#Fractional Scaling|Fractional Scaling]]

### wp_linux_dmabuf

Wayland protocol for sharing DMA-BUFs between clients and compositors. Enables zero-copy GPU buffer sharing.

**Components:**
- `wp_linux_dmabuf_v1`: Global factory
- `wp_linux_buffer_params_v1`: Plane information collector
- `wp_linux_dmabuf_feedback_v1`: Allocation hints

**See also:** [[#DMA-BUF|DMA-BUF]]

### wp_viewporter

Wayland protocol extension enabling decoupling of buffer size from surface size through cropping and scaling.

**Functions:**
- `set_source`: Define crop rectangle (can be fractional)
- `set_destination`: Define output size

**See also:** [[#Viewport Transform|Viewport Transform]]

### wlr_buffer

wlroots abstraction for buffer memory. Can represent DMA-BUF, shared memory, or CPU data. Implements reference counting for lifecycle management.

**Capabilities:**
- `WLR_BUFFER_CAP_DATA_PTR`: CPU-accessible
- `WLR_BUFFER_CAP_DMABUF`: GPU memory
- `WLR_BUFFER_CAP_SHM`: Shared memory

### wlr_client_buffer

wlroots structure representing a client's buffer attached to a surface. Manages texture import and buffer locking.

### wlr_dmabuf_attributes

Structure describing DMA-BUF parameters: format, modifier, plane count, file descriptors, offsets, and strides.

### wlr_output

wlroots abstraction for a display output (monitor). Manages resolution, refresh rate, scale, and physical connection state.

### wlr_render_pass

Modern wlroots (0.18+) rendering API for accumulating render operations before submission. Replaces immediate-mode rendering.

### wlr_renderer

wlroots abstraction for GPU rendering. Implementations include GLES2, Vulkan, and Pixman (software).

### wlr_scene

wlroots declarative scene graph API. Provides automatic damage tracking, visibility culling, and protocol implementations.

**Key types:**
- `wlr_scene_tree`: Grouping node
- `wlr_scene_buffer`: Buffer display node with scaling/cropping
- `wlr_scene_rect`: Solid color rectangle
- `wlr_scene_output`: Output viewport

**See also:** [[#Scene Graph|Scene Graph]]

### wlr_scene_buffer

Scene graph node for displaying a buffer with configurable scaling, cropping, and transforms.

**Key properties:**
- `opacity`: Alpha blending (0.0 - 1.0)
- `filter_mode`: `WLR_SCALE_FILTER_BILINEAR` or `WLR_SCALE_FILTER_NEAREST`
- `src_box`: Source crop rectangle
- `dst_width/dst_height`: Destination size (scaling)
- `transform`: Rotation/flip

### wlr_scene_output

Scene graph viewport representing an output. Controls where and how the scene is rendered on a physical display.

### wlr_scale_filter_mode

Enum controlling texture filtering in wlroots: `WLR_SCALE_FILTER_BILINEAR` (smooth) or `WLR_SCALE_FILTER_NEAREST` (pixelated).

### wlr_texture

wlroots abstraction for a GPU texture object. Bound to a specific renderer (GLES2/Vulkan). Created from buffers via import.

---

## Z

### Zero-Copy

Buffer sharing architecture avoiding CPU memory copies. Client renders to GPU memory; compositor displays directly from same memory via DMA-BUF.

**Requirements:**
- DMA-BUF support
- Compatible format/modifier between GPU and display
- Proper synchronization

**Benefits:** Lower latency, reduced CPU usage, better battery life

**See also:** [[#DMA-BUF|DMA-BUF]], [[#Direct Scanout|Direct Scanout]]

---

## Quick Reference Tables

### Scaling Algorithms Comparison

| Algorithm | Speed | Quality | Best For |
|-----------|-------|---------|----------|
| Nearest | 5/5 | 2/5 | Pixel art, integer scales |
| Bilinear | 5/5 | 3/5 | General purpose, video |
| Lanczos-2 | 4/5 | 4/5 | Real-time quality |
| Lanczos-3 | 3/5 | 5/5 | Maximum quality |
| FSR | 3/5 | 5/5 | Gaming upscaling |
| NIS | 4/5 | 4/5 | Fast upscaling |

### Wayland Coordinate Systems

| Coordinate System | Description | Example |
|-------------------|-------------|---------|
| Buffer | Raw pixel dimensions | 3840×2160 |
| Surface | Logical size after transforms | 1920×1080 |
| Output | Physical display pixels | 2560×1440 |

### Buffer Capabilities

| Capability | Description | Use Case |
|------------|-------------|----------|
| `DATA_PTR` | CPU-accessible | Software rendering |
| `DMABUF` | GPU memory | Zero-copy sharing |
| `SHM` | Shared memory | Simple clients |

---

*Last updated: 2026-04-09*
