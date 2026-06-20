# Navigation

> Keyboard-driven navigation for viewport and focus.

## Decision

- **Super + Arrow Keys** → Directional focus (jump to window in direction)
- **Super + Shift + Arrow Keys** → Camera pan (move viewport)

## Focus Navigation

### Directional Focus (Super + Arrows)

Find the nearest window in the specified direction using cone search:

```
                    ▲
                   /│\
                  / │ \     Search cone for "Up"
                 /  │  \    (90° angle)
                /   │   \
        ───────/────┼────\───────
              \     │     /
               \    │    /
                \   │   /
                 \  │  /
                  \ │ /
                   \│/
                    ▼
```

### Algorithm

1. Get current focused window center
2. Define search cone (90° angle in direction)
3. Find all windows with center inside cone
4. Select nearest one by Euclidean distance
5. If none found, search in wider cone (180°)

## Camera Navigation

### Pan (Super + Shift + Arrows)

Move the viewport camera without changing focus:

| Keybind | Action |
|---------|--------|
| `Super+Shift+H` | Pan left |
| `Super+Shift+J` | Pan down |
| `Super+Shift+K` | Pan up |
| `Super+Shift+L` | Pan right |

### Alternative: Arrow Keys

| Keybind | Action |
|---------|--------|
| `Super+Shift+Left` | Pan left |
| `Super+Shift+Down` | Pan down |
| `Super+Shift+Up` | Pan up |
| `Super+Shift+Right` | Pan right |

## Zoom Navigation

| Keybind | Action |
|---------|--------|
| `Super+equal` | Zoom in (1.1x) |
| `Super+minus` | Zoom out (0.9x) |
| `Super+BackSpace` | Reset view (x=0, y=0, zoom=1.0) |

## Quick Navigation

| Keybind | Action |
|---------|--------|
| `Super+Z` | Toggle follow focus mode |
| `Super+Shift+Z` | Toggle follow new windows |

## Visual Feedback

- Smooth camera transitions (spring physics)
- Edge indicators when windows exist off-screen
- Focus ring highlight on target window

## Related

- [[layout-paradigm]] - Window types and positioning
- [[../comparators/driftwm#Navigation System]] - Driftwm's cone search
- [[../comparators/niri-layout#Navigation]] - Niri's focus model
