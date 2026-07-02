# Viewport Navigation Decision

> How users pan, zoom, and navigate the infinite canvas.

## Status

**Status:** DECIDED — Implemented

## Decision

**Multi-modal navigation** combining directional, cone search, and zoom.

## Navigation Modes

### 1. Directional Focus

Move focus using arrow keys with automatic viewport following:

| Key | Action |
|-----|--------|
| `Super+Left/Right` | Focus column left/right |
| `Super+Up/Down` | Focus window up/down (in column) |
| `Super+Home` | Focus first column |
| `Super+End` | Focus last column |

**Viewport follows focused window** — smooth pan animation.

### 2. Cone Search

For anchored windows not in column strip, use cone search from current focus:

```
Current focus is B. Press Super+Right:

        A
         \
          \ 60° cone
    C ─────B───── D  → D selected (in cone, closest)
          /
         /
        E

D is in the cone and closest, so D gets focus.
```

| Key | Action |
|-----|--------|
| `Super+Shift+Arrow` | Cone search in direction |

### 3. Pan (Move Viewport)

Move viewport without changing focus:

| Key | Action |
|-----|--------|
| `Super+Alt+Arrows` | Pan viewport |
| `Super+Alt+H/J/K/L` | Pan left/down/up/right |

### 4. Zoom

| Key | Action |
|-----|--------|
| `Super+Plus/Minus` | Zoom in/out |
| `Super+0` | Reset zoom to 1.0 |
| `Super+Shift+M` | Toggle overview mode |

## Overview Mode

Zoom-out showing all windows:

```
Normal view:              Overview:
┌───────────┐            ┌─────────────────┐
│  Column   │            │ A │ B │ C │ D  │
│    B      │     →      │   │   │   │    │
│           │            │   │   │   │    │
└───────────┘            └─────────────────┘
                         Click to focus
```

## Zoom Constraints

| Constraint | Value | Purpose |
|------------|-------|---------|
| Min zoom | 0.1x | Don't get too small |
| Max zoom | 5.0x | Don't pixelate too much |
| Default | 1.0x | Normal scale |

## Implementation Notes

```c
// Viewport state
struct Viewport {
    float x, y;        // World coordinates of viewport center
    float zoom;        // Current zoom level
    float target_zoom; // For smooth animation
};

// Convert screen to world coordinates
float screen_to_world_x(int sx) {
    return viewport.x + (sx - monitor_width/2) / viewport.zoom;
}
```

## Consequences

**Pros:**
- Multiple ways to navigate
- Works for both column and anchored windows
- Overview mode provides context

**Cons:**
- Many keybinds to learn
- Cone search can be unpredictable

## Related

- [Window Placement](./window-placement.md) — Where windows appear
- [Niri Navigation](../../comparators/niri/navigation.md) — Reference implementation
