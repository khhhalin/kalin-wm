# Architecture

> System design patterns, implementation decisions, and architectural documentation.

## Subsections

| Document | Description |
|----------|-------------|
| [Decisions](./decisions/) | Active design specifications and decisions |
| [Memory Management](./memory.md) | Client lifecycle and memory patterns |
| [Coordinate Systems](./coordinates.md) | World, screen, surface coordinate transforms |
| [Animations](./animations.md) | Animation patterns and timing |
| [Navigation](./navigation.md) | Keyboard navigation schemes |

## Design Principles

1. **Simplicity over flexibility** — Fewer configuration options, sensible defaults
2. **Performance by default** — Zero-copy where possible, minimal overhead
3. **Predictable behavior** — Consistent window placement and navigation
4. **Progressive enhancement** — Core features work everywhere, extras where supported

---

## Current Focus Areas

### Layout System
- [Column-based tiling](./decisions/layout-column.md) — Vertical stacking in columns
- [Window placement](./decisions/window-placement.md) — Where new windows appear
- [Viewport navigation](./decisions/viewport-navigation.md) — Pan and zoom behavior

### Stability
- [Client lifecycle](./decisions/client-lifecycle.md) — Spawn, map, unmap, destroy
- [Error handling](./decisions/error-handling.md) — Graceful degradation
- [Resource cleanup](./memory.md) — Preventing leaks

---

*See [meta/implementation-guide.md](../reference/implementation-guide.md) for implementation patterns.*
