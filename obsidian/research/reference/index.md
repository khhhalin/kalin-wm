# kalin-wm Research Vault

> Comprehensive research on Wayland compositor development.

---

## Vault Structure

### [Architecture](../architecture/)
System design, patterns, and decisions.

- [Decisions](../architecture/decisions/) — Active design specifications
- [Layout System](../architecture/decisions/layout-column.md) — Column-based hybrid layout
- [Window Placement](../architecture/decisions/window-placement.md) — New window rules
- [Navigation](../architecture/decisions/viewport-navigation.md) — Pan, zoom, focus

### [Protocols](../protocols/)
Wayland protocol implementations.

- [Protocol Matrix](../protocols/protocol-matrix.md) — Implementation status
- Core protocols, fractional scaling, DMA-BUF

### [Rendering](../rendering/)
GPU rendering and scaling.

- [Scaling Algorithms](../rendering/scaling-algorithms/) — FSR, Lanczos, bilinear
- Buffer management, scene graph

### [Comparators](../comparators/)
Research on other compositors and window management paradigms.

- [Niri](../comparators/niri/) — Scrollable-tiling columns
- [DriftWM](../comparators/driftwm.md) — Infinite 2D canvas

### [Performance](../performance/)
Optimization patterns.

- Zero-copy, damage tracking, benchmarks

### [Reference](../reference/)
Quick references.

- Glossary, API reference, implementation guide

---

## Quick Links

### By Task

| Task | Start Here |
|------|------------|
| Implement zoom | [Scaling Algorithms](../rendering/scaling-algorithms/) |
| Add layout feature | [Niri Layout](../comparators/niri/) |
| Fix placement bug | [Window Placement](../architecture/decisions/window-placement.md) |
| Add protocol | [Protocol Matrix](../protocols/protocol-matrix.md) |

### By Component

| Component | Documentation |
|-----------|---------------|
| Viewport | [Navigation](../architecture/decisions/viewport-navigation.md) |
| Window placement | [Placement](../architecture/decisions/window-placement.md) |
| Scaling | [Algorithms](../rendering/scaling-algorithms/) |
| Columns | [Niri](../comparators/niri/) |

---

## Conventions

- `[[filename]]` — Wiki link to document
- `[[filename#heading]]` — Link to section
- `> [!note]` — Callout blocks
- Code blocks include language

---

*Last updated: 2026-04-20*
