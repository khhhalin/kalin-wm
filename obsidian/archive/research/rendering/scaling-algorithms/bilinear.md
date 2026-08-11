# Bilinear Filtering

> Standard texture filtering for smooth scaling.

## Overview

Bilinear filtering is the default scaling method in most graphics systems. It provides smooth, anti-aliased results suitable for general UI and text.

**Best for:** General UI elements, text at moderate scales, default behavior

---

## How It Works

Bilinear filtering performs linear interpolation between the four nearest texels:

```
Sample points:
    Q11 ---- R1 ---- Q21
     |               |
     |       X       |   X = sample point
     |               |
    Q12 ---- R2 ---- Q22

R1 = Q11 * (1 - x) + Q21 * x      (horizontal interpolate)
R2 = Q12 * (1 - x) + Q22 * x      (horizontal interpolate)

Final = R1 * (1 - y) + R2 * y     (vertical interpolate)
```

---

## Implementation

### OpenGL

```c
// Set bilinear filtering
 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
```

### wlroots

```c
// Set filter mode for scene buffer
wlr_scene_buffer_set_filter_mode(
    scene_buffer, 
    WLR_SCALE_FILTER_BILINEAR
);
```

### Vulkan

```c
VkSamplerCreateInfo sampler_info = {
    .magFilter = VK_FILTER_LINEAR,
    .minFilter = VK_FILTER_LINEAR,
    // ... other settings
};
```

---

## Characteristics

| Aspect | Value |
|--------|-------|
| Memory Fetches | 4 per sample |
| Performance | 5/5 Very fast (hardware) |
| Upscale Quality | 3/5 Smooth, slightly soft |
| Downscale Quality | 2/5 Can alias without mipmaps |
| Sharpness | Medium-soft |

---

## When to Use

| Scenario | Recommendation |
|----------|----------------|
| Text at 1.5x-2x | Yes Good balance |
| UI elements | Yes Default choice |
| Pixel art | No Use nearest neighbor |
| Photos | Caution: OK, Lanczos better |
| Gaming | Caution: OK, FSR better for performance |

---

## Related

- [Nearest Neighbor](./nearest.md) — Pixel-perfect scaling
- [Lanczos](./lanczos.md) — Higher quality sinc-based
- [Performance Comparison](./performance.md) — Benchmarks
