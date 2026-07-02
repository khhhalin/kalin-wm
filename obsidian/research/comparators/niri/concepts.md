# Niri Core Concepts

> Fundamental concepts of Niri's scrollable-tiling system.

## View Offset System

The scrolling is implemented through a **view offset** that translates the coordinate system:

```rust
// From src/layout/scrolling.rs
pub enum ViewOffset {
    Static(f64),           // Fixed position
    Animation(Animation),  // Smooth transition
    Gesture(ViewGesture),  // Touch/mouse gesture
}
```

The view offset determines which portion of the infinite canvas is visible.

---

## Center-Focused Column Modes

```rust
// From niri-config/src/layout.rs
pub enum CenterFocusedColumn {
    Never,       // Don't center focused column
    Always,      // Always center
    OnOverflow,  // Center only when doesn't fit
}
```

### Behavior

| Mode | Description |
|------|-------------|
| **Never** | Column stays where placed |
| **Always** | Focused column centered on screen |
| **OnOverflow** | Centers only when off-screen |

---

## Column Architecture

Windows are organized in **columns** (`Column<W>`):

```
Column Structure:
┌─────────────────┐
│    Window 1     │  ← Top of stack
│    (focused)    │
├─────────────────┤
│    Window 2     │
├─────────────────┤
│    Window 3     │
└─────────────────┘
     Column
```

Each column contains one or more stacked windows (tiles).

---

## Scrollable vs Traditional

```
Traditional Tiling (dwm-style):
┌─────────────────────────────────┐
│        │        │               │  All windows fit
│   A    │   B    │      C        │  within screen
│        │        │               │
└─────────────────────────────────┘

Niri Scrollable-Tiling:
┌─────────────────────────────────┬──────────┬──────────┐
│        │        │               │          │          │
│   A    │   B    │      C        │    D     │    E     │
│        │        │               │          │          │
└─────────────────────────────────┴──────────┴──────────┘
         ↑ Visible Area ↑
                  ← Scroll Direction →
```

---

*See [Placement](./placement.md) for window placement details.*
