# Rendering

> GPU rendering, scaling algorithms, and compositing techniques.

## Topics

| Area | Description |
|------|-------------|
| [Scaling Algorithms](./scaling-algorithms/) | FSR, NIS, Lanczos, bilinear filtering |
| [Buffer Management](./buffers/) | Buffer lifecycle, DMA-BUF, zero-copy |
| [Scene Graph](./scene-graph.md) | wlroots scene graph patterns |
| [Fractional Scaling](./fractional-scaling.md) | HiDPI and sub-pixel scaling |

---

## Quick Reference

### Filter Modes

| Mode | Quality | Performance | Best For |
|------|---------|-------------|----------|
| Nearest | Low | Fastest | Pixel art, sharp edges |
| Bilinear | Medium | Fast | General UI, text |
| Lanczos | High | Medium | Photos, smooth gradients |
| FSR | Very High | Medium-Slow | Gaming, real-time |

### Scaling Pipeline

```
Client Buffer → Transform → Scale → Composite → Output
     │              │         │          │         │
     │              │         │          │         ▼
     │              │         │          │    Display
     │              │         │          │
     ▼              ▼         ▼          ▼
   wlr_buffer   wlr_scene   Filter    wlr_renderer
```

---

*See [GPU Rendering](./scaling-algorithms/) for detailed algorithm analysis.*
