# Niri Animations

> Spring-based animation system for smooth transitions.

## Animation Types

```rust
// From niri-config/src/lib.rs
pub enum Animation {
    Spring(SpringParams),    // Physics-based
}

pub struct SpringParams {
    pub stiffness: f64,      // Spring stiffness (0-1)
    pub damping: f64,        // Damping ratio
}
```

---

## Configurable Animations

| Animation | Purpose | Default |
|-----------|---------|---------|
| `workspace-switch` | Vertical workspace transition | Spring(1.0, 1.0) |
| `window-open` | New window appearance | Spring(1.0, 1.0) |
| `window-close` | Window disappearance | Spring(1.0, 1.0) |
| `horizontal-view-movement` | Column scroll | Spring(1.0, 1.0) |
| `config-notification-open/close` | Config reload notification | Spring(1.0, 1.0) |

---

## View Offset Animation

When focusing a new column, the view offset animates:

```rust
pub enum ViewOffset {
    Static(f64),              // No animation
    Animation(Animation),     // Spring animation
    Gesture(ViewGesture),     // Interactive
}
```

### Animation Flow

```
User focuses column 3
         │
         ▼
┌────────────────┐
│ Calculate target│  offset for column 3
│ view offset     │
└────────────────┘
         │
         ▼
┌────────────────┐
│ Spring animate  │  from current to target
│ view offset     │
└────────────────┘
         │
         ▼
   View arrives
   at column 3
```

---

## Gestures

Touchpad and mouse gestures for interactive scrolling:

```rust
pub struct ViewGesture {
    pub delta_x: f64,        // Accumulated delta
    pub is_fling: bool,      // Fling gesture detected
}
```

### Gesture Types

| Gesture | Action |
|---------|--------|
| Horizontal swipe | Scroll columns |
| Vertical swipe | Switch workspaces |
| Fling | Continue with momentum |
| Pinch | (Future: zoom) |

---

## Spring Physics

Spring animations provide natural, responsive motion:

```rust
// Spring parameters affect feel
pub struct SpringParams {
    pub stiffness: f64,   // Higher = snappier
    pub damping: f64,     // Higher = less oscillation
}
```

### Preset Feelings

| Stiffness | Damping | Feel |
|-----------|---------|------|
| 1.0 | 1.0 | Balanced (default) |
| 2.0 | 1.0 | Snappy |
| 0.5 | 1.5 | Smooth, heavy |
| 1.0 | 0.5 | Bouncy |

---

*See [kalin-wm Animations](../../architecture/animations.md) for our implementation.*
