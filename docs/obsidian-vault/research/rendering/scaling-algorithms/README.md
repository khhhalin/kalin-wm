# Scaling Algorithms

> GPU scaling algorithms for high-quality texture upscaling and zoom.

## Algorithm Overview

| Algorithm | Type | Quality | Use Case |
|-----------|------|---------|----------|
| [Bilinear](./bilinear.md) | Linear interpolation | Medium | General UI, default |
| [Lanczos](./lanczos.md) | Sinc-based | High | Photos, video |
| [FSR](./fsr.md) | Edge reconstruction | Very High | Gaming, real-time |
| [NIS](./nis.md) | Detail sharpening | Very High | NVIDIA GPUs |

---

## Quick Comparison

```
Quality:    Nearest < Bilinear < Lanczos < FSR/NIS
Speed:      FSR/NIS < Lanczos < Bilinear < Nearest
VRAM Use:   All similar (depends on implementation)
```

### When to Use Each

| Scenario | Recommended | Why |
|----------|-------------|-----|
| Text/UI at 1.5x | Bilinear | Sharp but smooth |
| Photos at 2x | Lanczos | Preserves detail |
| Gaming at 1.5x | FSR | Reconstructs edges |
| Pixel art | Nearest | Preserves crisp pixels |

---

## Implementation

wlroots provides filter modes via `wlr_scene_buffer_set_filter_mode()`:

```c
enum wlr_scale_filter_mode {
    WLR_SCALE_FILTER_BILINEAR,
    WLR_SCALE_FILTER_NEAREST,
};

// Set filter for a buffer
wlr_scene_buffer_set_filter_mode(scene_buffer, WLR_SCALE_FILTER_BILINEAR);
```

---

## Sections

- [Bilinear Filtering](./bilinear.md) — Default smooth scaling
- [Lanczos Resampling](./lanczos.md) — High-quality sinc-based
- [AMD FSR](./fsr.md) — FidelityFX Super Resolution
- [NVIDIA NIS](./nis.md) — NVIDIA Image Scaling
- [Performance Comparison](./performance.md) — Benchmarks and trade-offs

---

*See [Buffer Management](../buffers/) for how buffers are prepared for scaling.*
