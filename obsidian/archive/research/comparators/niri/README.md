# Niri Scrollable-Tiling Layout

> Analysis of Niri's column-based scrollable window management.

Source: [YaLTeR/niri](https://github.com/YaLTeR/niri) (Rust + Smithay)

---

## Overview

Niri implements a **scrollable-tiling** paradigm where windows are arranged in horizontally scrollable columns rather than fitting within screen boundaries.

### Key Characteristics

| Aspect | Traditional Tiling | Niri Scrollable |
|--------|-------------------|-----------------|
| Window size | Constrained to screen | Fixed/proportional, unbounded |
| New window placement | Split existing space | Add to the right |
| Navigation | Focus only | Scroll + focus |
| Column count | Screen-limited | Unlimited |

---

## Subsections

| Topic | Document | Description |
|-------|----------|-------------|
| [Core Concepts](./concepts.md) | Fundamentals | View offset, centering modes |
| [Window Placement](./placement.md) | Where windows appear | Column stacking, sizing |
| [Navigation](./navigation.md) | Movement | Directional, overview mode |
| [Animations](./animations.md) | Transitions | Spring physics, gestures |

---

## Architecture

```rust
// Core types from Niri source
pub enum ViewOffset {
    Static(f64),           // Fixed position
    Animation(Animation),  // Smooth transition
    Gesture(ViewGesture),  // Touch/mouse gesture
}

pub enum CenterFocusedColumn {
    Never,       // Don't center
    Always,      // Always center
    OnOverflow,  // Center only when needed
}
```

---

## Visual Layout

```
Traditional Tiling:
┌─────────────────────────┐
│      │      │           │  All windows squeezed
│  A   │  B   │     C     │  to fit screen
│      │      │           │
└─────────────────────────┘

Niri Scrollable:
┌─────────────────┬────────┬────────┐
│      │      │   │        │        │
│  A   │  B   │ C │   D    │   E    │  ← Scrollable
│      │      │   │        │        │    canvas
└─────────────────┴────────┴────────┘
     ↑ Viewport ↑
```

---

## Key Features

- **Predictable placement**: New windows always open to the right
- **Smooth scrolling**: Spring-based animations
- **Overview mode**: Zoom-out view of all workspaces
- **Dual modes**: Tiling and floating coexist

---

*See [kalin-wm Layout Decisions](../../architecture/decisions/layout-column.md) for our adaptation.*
