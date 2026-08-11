# kalin-wm: Scope and Exit Conditions

> Comprehensive project scope document defining current state, exit conditions, feature priorities, and architectural decisions for kalin-wm.

---

## 1. Current State Assessment

### 1.1 Implemented Features

#### Core Viewport System Yes
| Feature | Status | Notes |
|---------|--------|-------|
| 2D World Coordinate System | Yes Complete | `world.x/y` in Client struct, persistent positions |
| Viewport Pan | Yes Complete | `Super+Ctrl+H/J/K/L` for directional panning |
| Viewport Zoom | Yes Complete | `Super+plus/minus`, range 0.1x - 5.0x |
| Zoom Reset | Yes Complete | `Super+Backspace` returns to 1.0x at origin |
| Coordinate Transform Macros | Yes Complete | `WORLD_TO_SCREEN_*` and `SCREEN_TO_WORLD_*` |
| Camera Follow Mode | Yes Complete | `Super+Z` toggles follow focused window |
| Auto-pan to New Windows | Yes Complete | `Super+Shift+Z` toggles automatic panning |

#### Infinite Layout Yes
| Feature | Status | Notes |
|---------|--------|-------|
| Infinite Place Algorithm | Yes Complete | Places windows beyond rightmost edge + spacing |
| Window Spacing | Yes Complete | `INFINITE_WINDOW_SPACING` (50px) between windows |
| World Position Persistence | Yes Complete | `world.set` flag tracks initialized windows |
| Rightmost Edge Detection | Yes Complete | `infinite_rightmost_edge()` finds placement point |
| Top Y Alignment | Yes Complete | `infinite_topmost_y()` aligns new window Y |

#### Buffer Scaling Yes
| Feature | Status | Notes |
|---------|--------|-------|
| Content Zoom | Yes Complete | `client_set_buffer_scale()` scales window content |
| Integer Zoom Detection | Yes Complete | `is_integer_zoom()` for filter selection |
| Nearest Filter | Yes Complete | Used for integer scales (1.0x, 2.0x, 0.5x) |
| Bilinear Filter | Yes Complete | Used for fractional scales (1.5x, 1.25x) |
| Automatic Scaling | Yes Complete | Called from `resize()` and `infinite()` |

#### Visual Features Yes
| Feature | Status | Notes |
|---------|--------|-------|
| Stationary Wallpaper | Yes Complete | Grid pattern fixed to screen (not canvas) |
| Window Borders | Yes Complete | Standard dwl border rendering |
| Focus Indication | Yes Complete | Border color changes on focus |
| Crop Mode | Yes Complete | `Super+C` for interactive crop selection |

#### Input Handling Yes
| Feature | Status | Notes |
|---------|--------|-------|
| Keyboard Navigation | Yes Complete | Standard dwl keybindings + viewport controls |
| Mouse Drag (Move/Resize) | Yes Complete | `Super+LMB/RMB` for move/resize |
| Focus Follows Mouse | Yes Complete | `sloppyfocus` configuration option |
| Pointer Constraints | Yes Complete | Full wlr_pointer_constraints support |

### 1.2 Partially Implemented Features

| Feature | Status | Gap Analysis |
|---------|--------|--------------|
| 2D Window Placement | Caution: Partial | Currently places only on X-axis (horizontal line). Y-axis placement not implemented - could use spiral/cone search like driftwm |
| Multi-Monitor | Caution: Partial | Basic multi-monitor works, but viewport is global per-monitor viewports not implemented |
| Tags/Workspaces | Caution: Partial | Traditional dwl tags work, but no infinite canvas workspace model |
| Directional Focus | Caution: Partial | `focusstack()` only does next/prev, no spatial direction navigation |

### 1.3 Missing Features (Not Implemented)

| Feature | Priority | Notes |
|---------|----------|-------|
| Overview Mode | High High | Zoom-out to see all windows (like Niri) |
| Animations | High High | Smooth pan/zoom transitions |
| Touchpad Gestures | Medium Medium | 2-finger pan, pinch-to-zoom |
| Cursor-Anchored Zoom | Medium Medium | Zoom toward cursor position, not center |
| Window Previews | Medium Medium | Thumbnail view of off-screen windows |
| Spatial Focus Navigation | Medium Medium | Cone search for directional window focus |
| Magnetic Snapping | Low Low | Edge alignment during drag |
| Edge Auto-Pan | Low Low | Auto-scroll when dragging to edge |
| Bookmarks/Named Positions | Low Low | Save/restore canvas positions |
| Zoom-to-Fit | Low Low | Auto-zoom to show all windows |
| Per-Window Zoom | Low Low | Different zoom levels per window |

---

## 2. Exit Conditions (Definition of Done)

### 2.1 Minimum Viable Product (MVP)

**MVP Criteria** - kalin-wm is usable as a daily driver:

- [x] **Basic infinite canvas** - Windows placed in world coordinates, extend beyond screen
- [x] **Viewport navigation** - Pan and zoom with keyboard shortcuts
- [x] **Buffer scaling** - Content zooms, not just positions
- [x] **Window management** - Focus, close, move, resize, fullscreen
- [x] **Multi-monitor support** - Basic multiple output handling
- [x] **XWayland support** - X11 applications work
- [x] **Layer shell** - Panels, notifications, wallpapers work
- [x] **Session lock** - Screen locking functional

**MVP Status**: Yes **ACHIEVED** (Current implementation meets MVP)

### 2.2 Feature Complete (v1.0)

**v1.0 Criteria** - kalin-wm is a complete infinite canvas compositor:

#### Core Layout (P0)
- [ ] **2D Window Placement** - Spiral or cone-search placement in both X and Y axes
- [ ] **Overview Mode** - Keybinding to zoom out and see all windows
- [ ] **Spatial Focus Navigation** - Directional focus (left/right/up/down) using cone search
- [ ] **Cursor-Anchored Zoom** - Zoom centered on mouse cursor position

#### Visual Polish (P1)
- [ ] **Smooth Animations** - Animated pan/zoom with configurable duration
- [ ] **Touchpad Gestures** - 2-finger pan, pinch zoom (libinput gestures)
- [ ] **Window Shadows** - Drop shadows for depth perception
- [ ] **Rounded Corners** - Configurable corner radius

#### Advanced Navigation (P1)
- [ ] **Zoom-to-Fit** - Auto-zoom to show all visible windows
- [ ] **Zoom-to-Window** - Auto-zoom to specific window size
- [ ] **Bookmarks** - Save/restore canvas positions with hotkeys
- [ ] **Minimap** - Small overview in corner showing viewport position

#### Productivity (P2)
- [ ] **Magnetic Snapping** - Windows snap to edges/corners
- [ ] **Smart Placement** - Avoid occlusion when placing new windows
- [ ] **Window Groups** - Tag-based window collections
- [ ] **Search/Filter** - Find and focus windows by title/app_id

### 2.3 Exit Condition Summary

| Phase | Condition | Status |
|-------|-----------|--------|
| **MVP** | Basic infinite canvas compositor usable daily | Yes Complete |
| **v1.0** | Feature-complete with overview, animations, gestures | 🔄 In Progress |
| **v2.0** | Advanced features, plugins, full configuration | Planned |

---

## 3. Feature/System List

### 3.1 Core Layout Systems

| System | Current | Target | Priority |
|--------|---------|--------|----------|
| Infinite Canvas | Yes Linear (X-axis) | 🎯 2D Spiral/Freeform | P0 |
| World Coordinates | Yes Per-window | 🎯 Persistent storage | P1 |
| Window Placement | Yes Rightmost+spacing | 🎯 Smart 2D placement | P0 |
| Column Layout | No None | 🎯 Niri-style optional | P2 |

### 3.2 Navigation

| Feature | Current | Target | Priority |
|---------|---------|--------|----------|
| Keyboard Pan | Yes 4-directional | 🎯 Same | P0 |
| Keyboard Zoom | Yes In/Out/Reset | 🎯 Same | P0 |
| Follow Focus | Yes Toggle | 🎯 Same | P0 |
| Follow New Windows | Yes Toggle | 🎯 Same | P0 |
| Cursor-Anchored Zoom | No Center zoom | 🎯 Cursor zoom | P0 |
| Touchpad Pan | No None | 🎯 2-finger swipe | P1 |
| Touchpad Zoom | No None | 🎯 Pinch gesture | P1 |
| Directional Focus | No Next/Prev only | 🎯 Cone search | P0 |
| Overview Mode | No None | 🎯 Zoom-out view | P0 |
| Bookmarks | No None | 🎯 Save positions | P2 |

### 3.3 Window Management

| Feature | Current | Target | Priority |
|---------|---------|--------|----------|
| Focus | Yes Standard | 🎯 Same | P0 |
| Stacking | Yes Standard | 🎯 Same | P0 |
| Floating | Yes Standard | 🎯 Same | P0 |
| Fullscreen | Yes Standard | 🎯 Same | P0 |
| Move/Resize | Yes Mouse | 🎯 Same | P0 |
| Close/Kill | Yes Standard | 🎯 Same | P0 |
| Crop | Yes Interactive | 🎯 Same | P1 |
| Minimize/Hide | No None | 🎯 Collapse to dot | P2 |
| Window Rules | Yes Basic | 🎯 Position rules | P1 |

### 3.4 Visual Features

| Feature | Current | Target | Priority |
|---------|---------|--------|----------|
| Wallpaper | Yes Grid pattern | 🎯 Configurable | P1 |
| Borders | Yes Standard | 🎯 Same | P0 |
| Focus Ring | Yes Border color | 🎯 Optional glow | P2 |
| Animations | No None | 🎯 Smooth pan/zoom | P1 |
| Shadows | No None | 🎯 Drop shadows | P1 |
| Rounded Corners | No None | 🎯 Configurable | P2 |
| Opacity | No None | 🎯 Per-window | P2 |
| Blur | No None | 🎯 Background | P3 |

### 3.5 Input Handling

| Feature | Current | Target | Priority |
|---------|---------|--------|----------|
| Keyboard | Yes Full | 🎯 Same | P0 |
| Mouse | Yes Full | 🎯 Same | P0 |
| Trackpad | Yes Basic | 🎯 Gestures | P1 |
| Pointer Constraints | Yes Full | 🎯 Same | P0 |

### 3.6 Multi-Monitor

| Feature | Current | Target | Priority |
|---------|---------|--------|----------|
| Basic Multi-Monitor | Yes Standard | 🎯 Same | P0 |
| Per-Monitor Viewport | No Shared | 🎯 Independent | P1 |
| Window Moving | Yes Standard | 🎯 Same | P0 |

---

## 4. Prioritization

### P0 (Critical - Must Have for v1.0)

These features are essential for kalin-wm to be considered a complete infinite canvas compositor:

1. **2D Window Placement** - Spiral or cone-search algorithm for placing windows in 2D space
2. **Overview Mode** - Zoom out to see all windows (like Niri's overview)
3. **Spatial Directional Focus** - Navigate to nearest window in a direction
4. **Cursor-Anchored Zoom** - Zoom centered on cursor, not screen center
5. **Smooth Animations** - Animated transitions for pan/zoom

### P1 (Important - Should Have for v1.0)

These features significantly improve the user experience:

1. **Touchpad Gestures** - 2-finger pan, pinch-to-zoom
2. **Zoom-to-Fit** - Auto-zoom to show all windows
3. **Zoom-to-Window** - Center and zoom to specific window
4. **Bookmarks** - Save/restore canvas positions
5. **Window Shadows** - Visual depth cues
6. **Persistent World State** - Save/restore window positions
7. **Smart Placement** - Avoid window occlusion

### P2 (Nice to Have - Post v1.0)

These features add polish but aren't essential:

1. **Rounded Corners** - Configurable corner radius
2. **Per-Window Opacity** - Transparency control
3. **Magnetic Snapping** - Edge alignment
4. **Minimap** - Corner overview map
5. **Window Groups** - Tag-based collections
6. **Column Layout Mode** - Niri-style optional layout

### P3 (Future Considerations)

These features are for future versions:

1. **Background Blur** - Frosted glass effect
2. **Custom Shaders** - GLSL background effects
3. **Plugin System** - Extension architecture
4. **Remote Canvas** - Network-transparent windows

---

## 5. Architectural Decisions Needed

### 5.1 Layout Paradigm Decision

**Question**: Should kalin-wm use Niri-style columns or driftwm-style freeform?

**Options**:

| Option | Pros | Cons |
|--------|------|------|
| **A: Pure Freeform (driftwm)** | Maximum flexibility, true infinite canvas | Can become messy, harder to navigate |
| **B: Column-Based (Niri)** | Predictable layout, easy navigation | Less flexibility, 1D scrolling only |
| **C: Hybrid (Recommended)** | Columns on infinite canvas, best of both | More complex implementation |

**Recommendation**: **Option C - Hybrid**
- Implement freeform 2D placement as default
- Add optional column mode (like Niri) as layout option
- Allow windows to be organized in columns but positioned anywhere on canvas

### 5.2 Window Auto-Pan Behavior

**Question**: When should the camera automatically pan?

**Options**:

| Trigger | Behavior | Configurable |
|---------|----------|--------------|
| New window opens | Pan to show new window | Yes Already implemented |
| Focus change | Pan to center focused window | Yes Already implemented |
| Window moves off-screen | Pan to keep window visible | Caution: Decision needed |
| User approaches edge | Auto-pan to reveal more | Caution: Decision needed |

**Recommendation**: 
- Keep existing follow_new_windows and follow_focus options
- Add `edge_auto_pan` option for drag operations
- Add `keep_focus_visible` option for window moves

### 5.3 Navigation Patterns

**Question**: What navigation patterns should be primary?

| Pattern | Use Case | Priority |
|---------|----------|----------|
| Pan viewport | Explore canvas | Primary |
| Directional focus | Jump to window | Primary |
| Overview mode | Global view | Primary |
| Zoom in/out | Detail vs context | Primary |
| Bookmarks | Quick positions | Secondary |
| Search | Find window | Secondary |

**Recommendation**:
- Primary: Pan + Directional Focus + Overview + Zoom
- Secondary: Bookmarks + Search
- Follow Vim-style navigation philosophy

### 5.4 Overview Mode Design

**Question**: Should there be an overview mode?

**Options**:

| Option | Description |
|--------|-------------|
| **A: No Overview** | Just pan and zoom manually |
| **B: Zoom-to-Fit** | Single action to see all windows |
| **C: Full Overview (Niri-style)** | Toggle mode with all windows visible |
| **D: Minimap** | Always-visible corner overview |

**Recommendation**: **Option C + D**
- Full overview mode as primary (Super+W)
- Optional minimap for power users
- Configurable overview zoom level

### 5.5 Coordinate System Boundaries

**Question**: Should the infinite canvas have boundaries?

| Option | Pros | Cons |
|--------|------|------|
| **Truly Infinite** | No limits, pure concept | Potential floating-point precision issues at extreme distances |
| **Soft Boundaries** | Warn/visual indication when far | Slightly complex |
| **Hard Boundaries** | Simple, prevents issues | Limiting |

**Recommendation**: **Soft Boundaries**
- Very large soft limit (e.g., ±100000 pixels)
- Visual indicator when approaching limit
- Option to reset to origin

---

## 6. Research References

This document is informed by the following research:

- [[../comparators/niri-layout|Niri Layout Analysis]] - Scrollable-tiling paradigm
- [[../comparators/driftwm|Driftwm Analysis]] - Infinite canvas paradigm
- [[../reference/wlroots/scene-graph-scaling|Scene Graph Scaling]] - Technical implementation
- [[../reference/implementation-guide|Implementation Guide]] - Step-by-step patterns
- [[../reference/implementation-notes|Implementation Notes]] - Current implementation status

---

## 7. Development Roadmap

### Phase 1: Core v1.0 (Current - ~4 weeks)

**Focus**: P0 features for complete infinite canvas experience

1. Implement 2D spiral window placement
2. Add overview mode (zoom out to see all)
3. Implement spatial directional focus (cone search)
4. Add cursor-anchored zoom
5. Add smooth animations for pan/zoom

### Phase 2: Polish v1.0 (~2 weeks)

**Focus**: P1 features for refined experience

1. Touchpad gesture support
2. Zoom-to-fit and zoom-to-window
3. Bookmarks system
4. Window shadows
5. Persistent world state

### Phase 3: Release (~1 week)

**Focus**: Testing, documentation, release

1. Comprehensive testing
2. Documentation update
3. Release v1.0

---

## 8. Summary

**Current Status**: MVP Complete Yes

kalin-wm is currently a functional infinite canvas compositor with:
- 1D horizontal infinite layout
- Viewport pan/zoom with keyboard
- Buffer scaling for content zoom
- Basic camera following

**Next Milestone**: v1.0 Feature Complete

To reach v1.0, focus on:
1. 2D window placement (spiral algorithm)
2. Overview mode for global window view
3. Spatial navigation (cone search)
4. Cursor-anchored zoom
5. Smooth animations

**Decision Summary**:
- **Layout**: Hybrid (freeform + optional columns)
- **Auto-pan**: Keep existing, add edge auto-pan
- **Navigation**: Pan + Directional + Overview + Zoom
- **Overview**: Full overview mode + optional minimap
- **Boundaries**: Soft boundaries at ±100000px

---

*Document Version: 1.0*
*Last Updated: 2026-04-10*
*Related: [[../reference/implementation-notes|Implementation Notes]], [[../reference/implementation-guide|Implementation Guide]]*
