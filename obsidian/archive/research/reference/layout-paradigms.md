# Layouts

> Window management paradigms and compositor layout systems.

## Layout Paradigms

| Paradigm | Compositor | Best For |
|----------|------------|----------|
| [Scrollable-Tiling](./niri/) | Niri | Column workflows, predictable placement |
| [Infinite 2D Canvas](./driftwm.md) | DriftWM | Spatial organization, creative workflows |
| [Traditional Tiling](https://dwm.suckless.org/) | dwm, Sway | Screen-fitting layouts |

---

## Key Concepts

### World vs Screen Coordinates

```
World Space (infinite)     Screen Space (fixed)
┌─────────────────────┐    ┌──────────┐
│  A  │  B  │  C  │   │    │  B  │  C │  ← Viewport
│     │     │     │ D │    │     │    │     shows B,C
└─────────────────────┘    └──────────┘
       ↑
    View offset
```

### Navigation Patterns

| Pattern | Implementation | Keys |
|---------|----------------|------|
| Directional | Cone search from focus | Arrow keys |
| Sequential | Next/previous in list | Tab, Shift+Tab |
| Spatial | Direct position jump | Number keys |
| Overview | Zoom-out + click | Special key |

---

## Compositor Zoom Features

See [Zoom Patterns](./zoom-patterns.md) for how different compositors implement zoom.

| Feature | Hyprland | Niri | Wayfire |
|---------|----------|------|---------|
| Global Zoom | Yes | Yes | Yes |
| Per-Window | Yes | No | Yes |
| Smooth | Yes | Yes | Yes |
| Filter Modes | Config | Bilinear | Config |

---

*See [Architecture/Decisions](../architecture/decisions/) for kalin-wm layout decisions.*
