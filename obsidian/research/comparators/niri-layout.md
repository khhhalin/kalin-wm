# Niri Scrollable-Tiling Layout System

**Key Terms:** [[definitions#Wayland|Wayland]] | [[definitions#Smithay|Smithay]] | [[definitions#Scene Graph|Scene Graph]] | [[definitions#Fractional Scaling|Fractional Scaling]] | [[zoom-features#Niri|Overview Mode]]

> Comprehensive analysis of Niri's scrollable-tiling window management paradigm
> Date: 2026-04-10
> Source: [YaLTeR/niri](https://github.com/YaLTeR/niri) (Rust + Smithay)

---

## Executive Summary

Niri implements a **scrollable-tiling** window management paradigm that fundamentally differs from traditional tiling compositors. Rather than constraining windows to fit within the screen boundaries, Niri arranges windows in an **infinitely scrollable horizontal canvas** organized into columns. This approach provides:

- **Predictable window placement**: New windows always open to the right of the active column
- **Natural navigation**: Scroll or swipe to reveal additional columns beyond the viewport
- **Dual layout modes**: Scrolling (tiling) and floating coexist within each workspace
- **Smooth animations**: Spring-based physics for all layout transitions
- **Overview mode**: Zoom-out view of all workspaces with direct manipulation

---

## 1. Scrollable-Tiling Paradigm

### Core Concept

Traditional tiling compositors (dwm, i3, Sway) arrange windows to fit within the screen boundaries, splitting space either dynamically or through user-defined layouts. Niri takes a fundamentally different approach:

```
Traditional Tiling (dwm-style):
┌─────────────────────────────────┐
│        │        │               │  All windows fit within
│   A    │   B    │      C        │  screen boundaries
│        │        │               │  (may become too small)
└─────────────────────────────────┘

Niri Scrollable-Tiling:
┌─────────────────────────────────┬──────────┬──────────┐
│        │        │               │          │          │
│   A    │   B    │      C        │    D     │    E     │  ← Scrollable
│        │        │               │          │          │    viewport
└─────────────────────────────────┴──────────┴──────────┘
         ↑ Visible Area ↑
                  ← Scroll Direction →
```

### Key Characteristics

| Aspect | Traditional Tiling | Niri Scrollable-Tiling |
|--------|-------------------|----------------------|
| Window size | Constrained to fit screen | Fixed or proportional, unbounded |
| New window placement | Split existing space | Add to the right (or stack) |
| Navigation | Focus changes only | Scroll + focus changes |
| Column count | Limited by screen space | Unlimited (scrollable) |
| Visual context | All windows visible | Active column(s) visible, others scrolled |

### View Offset System

The scrolling is implemented through a **view offset** that translates the coordinate system:

```rust
// From src/layout/scrolling.rs
#[derive(Debug)]
pub(super) enum ViewOffset {
    /// The view offset is static (not animating).
    Static(f64),
    /// The view offset is animating between columns.
    Animation(Animation),
    /// The view offset is controlled by an ongoing gesture.
    Gesture(ViewGesture),
}
```

The view offset determines which portion of the infinite canvas is currently visible. When focusing a different column, the view offset animates smoothly to bring that column into view.

### Center-Focused Column Modes

Niri provides configurable behavior for how focused columns are positioned:

```rust
// From niri-config/src/layout.rs
pub enum CenterFocusedColumn {
    /// Focusing a column will not center the column.
    Never,
    /// The focused column will always be centered.
    Always,
    /// Focusing a column will center it if it doesn't fit on the screen 
    /// together with the previously focused column.
    OnOverflow,
}
```

---

## 2. Window Placement

### Column-Based Architecture

Windows are organized in **columns** (`Column<W>`), where each column contains one or more stacked windows (tiles):

```rust
// From src/layout/scrolling.rs
#[derive(Debug)]
pub struct Column<W: LayoutElement> {
    /// Tiles in this column (must be non-empty).
    tiles: Vec<Tile<W>>,
    /// Extra per-tile data.
    data: Vec<TileData>,
    /// Index of the currently active tile.
    active_tile_idx: usize,
    /// Desired width of this column.
    width: ColumnWidth,
    /// Whether this column is full-width.
    is_full_width: bool,
    /// Display mode (normal, tabbed, etc.).
    display_mode: ColumnDisplay,
    // ...
}
```

### New Window Placement Rules

When a new window opens, it follows these placement rules:

1. **Default placement**: Opens as a new column to the right of the currently active column
2. **Window rules**: Can override placement (specific output, workspace, floating)
3. **Target specification**: Can open next to a specific existing window
4. **Column width**: Uses preset proportions or fixed sizes

```rust
// From src/layout/scrolling.rs
pub fn add_tile(
    &mut self,
    col_idx: Option<usize>,  // Specific column index, or None for auto
    tile: Tile<W>,
    activate: bool,
    width: ColumnWidth,
    is_full_width: bool,
    anim_config: Option<niri_config::Animation>,
) {
    let column = Column::new_with_tile(tile, /* ... */);
    self.add_column(col_idx, column, activate, anim_config);
}
```

### Preset Widths

Columns can have predefined widths configured in the config:

```kdl
// Default Niri configuration
layout {
    preset-column-widths {
        proportion 0.33333
        proportion 0.5
        proportion 0.66667
    }
    default-column-width { proportion 0.5 }
}
```

Width options:
- **Proportion**: Percentage of the working area width (e.g., `0.5` = 50%)
- **Fixed**: Absolute pixel width (e.g., `Fixed(800)`)

---

## 3. Window Stacking and Focus

### Within-Column Stacking

Multiple windows can occupy the same column, stacked vertically:

```
Column Layout (tabbed mode):
┌─────────────────────┐
│ [Tab 1] [Tab 2]     │  ← Tab indicator
├─────────────────────┤
│                     │
│   Window Content    │  ← Active window visible
│                     │
└─────────────────────┘

Column Layout (stacked mode):
┌─────────────────────┐
│   Window 1 (top)    │
├─────────────────────┤
│   Window 2 (bottom) │
└─────────────────────┘
```

### Focus Navigation

Niri provides comprehensive focus navigation commands:

| Direction | Behavior |
|-----------|----------|
| `focus-left` / `focus-right` | Move to previous/next column |
| `focus-up` / `focus-down` | Move to window above/below in column |
| `focus-column-first` / `focus-column-last` | Jump to first/last column |
| `focus-window-top` / `focus-window-bottom` | Jump to top/bottom window in column |

### Focus Ring and Visual Feedback

```rust
// From src/layout/tile.rs
pub struct Tile<W: LayoutElement> {
    /// The toplevel window itself.
    window: W,
    /// The border around the window.
    border: FocusRing,
    /// The focus ring around the window.
    focus_ring: FocusRing,
    /// The shadow around the window.
    shadow: Shadow,
    // ...
}
```

Focus visualization includes:
- **Focus ring**: Colored border indicating active window
- **Border**: Window decorations (can be disabled per-window)
- **Shadow**: Drop shadow for depth perception
- **Tab indicator**: Visual tabs for tabbed columns

---

## 4. Workspace/Column Model

### Hierarchical Structure

Niri's layout follows a four-level hierarchy:

```
Layout<Mapped>
├── MonitorSet
│   └── Monitor (per output)
│       └── workspaces: Vec<Workspace>
│           └── Workspace
│               ├── scrolling: ScrollingSpace
│               │   └── columns: Vec<Column>
│               │       └── Column
│               │           └── tiles: Vec<Tile>
│               │               └── Tile
│               │                   └── window: Mapped
│               └── floating: FloatingSpace
│                   └── tiles: Vec<Tile>
└── (interactive_move, overview state, etc.)
```

### Workspace Structure

Each workspace contains two independent layouts:

```rust
// From src/layout/workspace.rs
#[derive(Debug)]
pub struct Workspace<W: LayoutElement> {
    /// The scrollable-tiling layout.
    scrolling: ScrollingSpace<W>,
    /// The floating layout.
    floating: FloatingSpace<W>,
    /// Whether the floating layout is active.
    floating_is_active: FloatingActive,
    /// Original output (for output reconnection logic).
    original_output: OutputId,
    /// Current output.
    output: Option<Output>,
    // ...
}
```

### Dynamic Workspaces

Niri uses a **dynamic workspace** system (similar to GNOME):

- Workspaces are created automatically as needed
- Empty workspaces are cleaned up (except the last one)
- Named workspaces can be configured in advance
- The last workspace is always empty (for quick window opening)

```rust
// From src/layout/monitor.rs
pub fn clean_up_workspaces(&mut self) {
    // Remove empty unnamed workspaces, keeping the last one
    for idx in (range_start..self.workspaces.len() - 1).rev() {
        if self.active_workspace_idx == idx {
            continue;
        }
        if !self.workspaces[idx].has_windows_or_name() {
            self.workspaces.remove(idx);
            // Adjust active index if needed
        }
    }
}
```

### Original Output Tracking

Workspaces remember their "original" output for intelligent behavior when outputs disconnect/reconnect:

```rust
// From src/layout/mod.rs module documentation
// Every workspace keeps track of which output it originated on—its 
// *original output*. When an output disconnects, its workspaces are 
// appended to the primary output, but remember their original output. 
// Then, if the original output connects again, all workspaces originally 
// from there move back to that output.
```

---

## 5. Overview Mode

### Concept

Overview mode is Niri's zoom-out workspace navigation feature. It scales down all workspaces to fit on screen, allowing visual overview and direct manipulation:

```
Normal Mode:                    Overview Mode:
┌─────────────────────┐         ┌─────────────────────────────────────┐
│                     │         │ ┌─────┐  ┌─────┐  ┌─────┐          │
│   Single workspace  │   →→→   │ │ WS1 │  │ WS2 │  │ WS3 │  ...     │
│   visible at 100%   │         │ │     │  │     │  │     │          │
│                     │         │ └─────┘  └─────┘  └─────┘          │
└─────────────────────┘         └─────────────────────────────────────┘
                                (all workspaces visible at 50% zoom)
```

### Implementation

```rust
// From src/layout/mod.rs
#[derive(Debug)]
pub struct Layout<W: LayoutElement> {
    /// Whether the overview is open.
    overview_open: bool,
    /// The overview zoom progress (1.0 = fully in overview).
    overview_progress: Option<OverviewProgress>,
    // ...
}

#[derive(Debug)]
enum OverviewProgress {
    Animation(Animation),
    Gesture(OverviewGesture),
    Open,
}
```

### Zoom Calculation

```rust
// From src/layout/mod.rs
fn compute_overview_zoom(options: &Options, overview_progress: Option<f64>) -> f64 {
    // Clamp to sane values (0.0001 - 0.75)
    let zoom = options.overview.zoom.clamp(0.0001, 0.75);
    
    if let Some(p) = overview_progress {
        // Interpolate between normal (1.0) and overview zoom
        (1. - p * (1. - zoom)).max(0.0001)
    } else {
        1.  // Normal mode
    }
}
```

Default overview zoom is **0.5** (50% scale), configurable via:

```kdl
overview {
    zoom 0.5
    backdrop-color "#000000"
}
```

### Workspace Rendering in Overview

```rust
// From src/layout/monitor.rs
pub fn workspaces_render_geo(&self) -> impl Iterator<Item = Rectangle<f64, Logical>> {
    let zoom = self.overview_zoom();
    let ws_size = self.workspace_size(zoom);
    let gap = self.workspace_gap(zoom);
    let ws_height_with_gap = ws_size.h + gap;
    
    // Compute position for each workspace in the overview
    let first_ws_y = -self.workspace_render_idx() * ws_height_with_gap;
    
    (0..=self.workspaces.len()).map(move |idx| {
        let y = first_ws_y + idx as f64 * ws_height_with_gap;
        Rectangle::new(loc, ws_size)
    })
}
```

### Interactive Features

Overview mode supports:
- **Click to activate**: Click any window to focus and exit overview
- **Drag to move**: Drag windows between workspaces
- **Touchpad gestures**: Swipe to scroll through workspaces
- **DnD support**: Drag-and-drop files between windows

### Layer Handling During Overview

```rust
// Background and bottom layers zoom with the overview
// Top and overlay layers stay fixed (for panels, notifications)
```

---

## 6. Architecture

### Key Source Files

| File | Purpose |
|------|---------|
| `src/layout/mod.rs` | Main `Layout` struct, coordinate system, overview |
| `src/layout/monitor.rs` | `Monitor` - output management, workspace switching |
| `src/layout/workspace.rs` | `Workspace` - scrolling + floating spaces |
| `src/layout/scrolling.rs` | `ScrollingSpace` - scrollable-tiling implementation |
| `src/layout/floating.rs` | `FloatingSpace` - floating window management |
| `src/layout/tile.rs` | `Tile` - window wrapper with decorations |
| `src/layout/column.rs` | `Column` - vertical window stack |

### Data Flow Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Niri Compositor                          │
├─────────────────────────────────────────────────────────────┤
│  Wayland Protocol Handlers (xdg_shell, layer_shell, ...)   │
├─────────────────────────────────────────────────────────────┤
│  Layout Engine                                              │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────────────┐ │
│  │   Layout    │──│   Monitor    │──│     Workspace       │ │
│  │  (coord sys)│  │ (per output) │  │ (scrolling+floating)│ │
│  └─────────────┘  └──────────────┘  └─────────────────────┘ │
│                                              │              │
│                    ┌─────────────────────────┘              │
│                    ▼                                        │
│  ┌──────────────────────────┐  ┌─────────────────────┐     │
│  │    ScrollingSpace        │  │   FloatingSpace     │     │
│  │  (columns, view_offset)  │  │ (positioned tiles)  │     │
│  └──────────────────────────┘  └─────────────────────┘     │
│           │                              │                 │
│           ▼                              ▼                 │
│  ┌─────────────────────────────────────────────────────┐  │
│  │                      Tile                            │  │
│  │  (window + border + focus_ring + shadow + animations)│  │
│  └─────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│  Rendering (Smithay + wlr_scene)                           │
└─────────────────────────────────────────────────────────────┘
```

### Rendering Pipeline

Niri uses Smithay's `wlr_scene` for efficient rendering:

```rust
// From src/niri.rs
niri_render_elements! {
    LayoutElementRenderElement<R> => {
        Wayland = WaylandSurfaceRenderElement<R>,
        SolidColor = SolidColorRenderElement,
    }
}
```

The rendering chain for overview:

```
WorkspaceRenderElement
  ├─ ScrollingSpaceRenderElement
  │   └─ TileRenderElement
  │       ├─ LayoutElementRenderElement (window content)
  │       ├─ BorderRenderElement
  │       ├─ FocusRingRenderElement
  │       └─ ShadowRenderElement
  └─ FloatingSpaceRenderElement (similar structure)

MonitorRenderElement (with overview transforms)
  ├─ RescaleRenderElement (zoom)
  ├─ RelocateRenderElement (position)
  └─ CropRenderElement (clipping)
```

### Animation System

Niri uses a spring-physics-based animation system:

```rust
// From src/animation/mod.rs
#[derive(Debug, Clone, Copy)]
pub enum Animation {
    /// Spring physics animation.
    Spring(SpringAnimation),
    /// Bezier curve animation.
    Bezier(BezierAnimation),
}
```

All layout transitions animate smoothly:
- Window open/close
- Column focus changes (view offset animation)
- Workspace switches
- Overview enter/exit
- Window resizing

### Transaction System

Niri uses transactions for frame-perfect window state changes:

```rust
// From src/utils/transaction.rs
pub struct Transaction {
    blockers: Vec<TransactionBlocker>,
}

impl Transaction {
    pub fn blockers_cleared(&self) -> bool {
        self.blockers.iter().all(|b| b.is_cleared())
    }
}
```

This ensures that window resizes and state changes are synchronized with the compositor's frame submission.

---

## 7. Comparison with Traditional Tiling

### dwl/dwm Style (Traditional)

```c
// dwl-style: windows arranged in a master/stack layout
// All windows must fit on screen
void arrange(Monitor *m) {
    // Calculate geometry for each window
    // Split screen space among visible windows
    resize(c, x, y, w, h, 0);  // Force window into position
}
```

**Characteristics:**
- Windows are resized to fit available space
- Master/stack or grid layouts
- Fixed number of visible windows
- Layout changes repositions all windows

### Niri Style (Scrollable)

```rust
// Niri: windows have fixed/proportional sizes
// View scrolls to show different portions
pub fn animate_view_offset_to_column(&mut self, idx: usize) {
    let new_view_offset = self.compute_new_view_offset_for_column(idx);
    self.view_offset = ViewOffset::Animation(Animation::new(
        self.clock.clone(),
        self.view_offset.current(),
        new_view_offset,
        0.,
        self.options.animations.horizontal_view_movement.0,
    ));
}
```

**Characteristics:**
- Windows maintain their sizes (unless user resizes)
- View offset scrolls to reveal columns
- Unlimited columns
- New windows don't affect existing window positions

### Feature Comparison

| Feature | dwl/dwm | i3/Sway | Niri |
|---------|---------|---------|------|
| Layout paradigm | Dynamic tiling | Manual tiling | Scrollable-tiling |
| Window sizing | Automatic fit | User-controlled | Preset proportions |
| New window impact | Rearranges all | Splits container | Adds to right |
| Column limit | Screen-fitted | Unlimited | Unlimited + scrollable |
| Overview mode | None | None | Built-in zoom-out |
| Workspace model | Fixed | Fixed | Dynamic |
| Floating support | Basic | Full | Full (per-workspace) |
| Animations | None | None | Spring-physics |

---

## 8. Configuration Examples

### Basic Layout Configuration

```kdl
layout {
    // Gaps between windows
    gaps 16
    
    // Preset column widths (cyclable)
    preset-column-widths {
        proportion 0.33333
        proportion 0.5
        proportion 0.66667
    }
    
    // Default width for new columns
    default-column-width { proportion 0.5 }
    
    // Focus behavior
    center-focused-column "never"  // or "always", "on-overflow"
    always-center-single-column false
    
    // Visual elements
    border {
        off false
        width 2
    }
    
    focus-ring {
        width 2
    }
    
    shadow {
        on true
    }
}
```

### Overview Configuration

```kdl
overview {
    // Zoom level (0.0 - 1.0)
    zoom 0.5
    
    // Background color
    backdrop-color "#1a1a1a"
    
    // Workspace shadows
    workspace-shadow {
        on true
        color "#000000"
    }
}
```

### Animation Configuration

```kdl
animations {
    // Window movement between columns
    horizontal-view-movement {
        spring damping-ratio=1.0 stiffness=800
    }
    
    // Workspace switching
    workspace-switch {
        spring damping-ratio=1.0 stiffness=800
    }
    
    // Overview open/close
    overview-open-close {
        spring damping-ratio=1.0 stiffness=800
    }
    
    // Window open/close
    window-open {
        duration-ms 150
        curve "ease-out"
    }
}
```

---

## 9. Key Insights for kalin-wm

### Strengths of Niri's Approach

1. **Predictable UX**: Users always know where new windows will appear
2. **Size preservation**: Windows maintain their preferred sizes
3. **Natural scrolling**: Horizontal scrolling aligns with modern touchpad gestures
4. **Overview integration**: Seamless zoom-out navigation
5. **Dual layout modes**: Scrolling + floating coexist naturally

### Implementation Considerations

1. **View offset management**: Requires careful coordinate transformation
2. **Animation complexity**: Smooth scrolling requires spring physics
3. **Input handling**: Must account for zoom in overview mode
4. **Damage tracking**: Scrolling requires efficient damage regions
5. **Memory usage**: Unlimited columns could lead to many off-screen windows

### Reusable Patterns

```rust
// Pattern: View offset with gesture support
pub enum ViewOffset {
    Static(f64),
    Animation(Animation),
    Gesture(GestureState),
}

// Pattern: Dual layout spaces
pub struct Workspace {
    scrolling: ScrollingSpace,
    floating: FloatingSpace,
    active_layout: LayoutType,
}

// Pattern: Original output tracking
pub struct Workspace {
    original_output: OutputId,  // For output reconnection
    current_output: Option<Output>,
}
```

---

## References

- [Niri GitHub Repository](https://github.com/YaLTeR/niri)
- [Niri Wiki - Layout](https://github.com/YaLTeR/niri/wiki/Layout)
- [Niri Configuration Documentation](https://github.com/YaLTeR/niri/wiki/Configuration)
- [Smithay Documentation](https://smithay.github.io/)
- [[definitions|Technical Definitions Glossary]]
- [[zoom-features|Wayland Compositor Zoom Features]]
- [[scene-graph-scaling|Scene Graph API]]
