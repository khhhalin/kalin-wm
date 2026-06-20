# Exit Conditions

> Definition of done for kalin-wm phases.

## MVP - COMPLETE

Minimum viable product - usable daily driver.

- [x] Infinite canvas with world coordinates
- [x] Viewport pan/zoom
- [x] Buffer scaling
- [x] Basic window management
- [x] Multi-monitor support
- [x] Crop mode

## v1.0 - IN PROGRESS

Feature-complete infinite canvas compositor.

### P0 (Critical)

- [x] 2D window placement (column + anchored)
- [x] Directional focus navigation
- [x] Camera pan with Super+Shift+Arrows
- [ ] Overview mode (deferred decision)

### P1 (Important)

- [ ] Smooth animations
- [ ] Touchpad gestures
- [ ] Window shadows
- [ ] Persistent world state

### P2 (Nice to Have)

- [ ] Rounded corners
- [ ] Minimap
- [ ] Bookmarks
- [ ] Magnetic snapping

## Checklist

### Layout System
- [x] Column placement working
- [x] Anchor window keybind (Super+Shift+A)
- [x] Re-columnize keybind (Super+Shift+C)
- [ ] Move anchored window

### Navigation
- [x] Super+Arrows = directional focus
- [x] Super+Shift+Arrows = pan
- [x] Cone search algorithm
- [ ] Visual focus indicator

### Visual
- [ ] Smooth pan animation
- [ ] Smooth zoom animation
- [ ] Focus ring/border
- [ ] Off-screen indicators

---

*See [[index]] for design documents.*
