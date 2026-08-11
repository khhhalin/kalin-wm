# Driftwm Infinite Canvas Wayland Compositor

**Key Terms:** [[definitions#Wayland|Wayland]] | [[definitions#Smithay|Smithay]] | [[definitions#Scene Graph|Scene Graph]] | [[definitions#Infinite Canvas|Infinite Canvas]] | [[definitions#Viewport|Viewport]] | [[niri-layout|Niri]]

> Comprehensive analysis of driftwm's infinite 2D canvas window management paradigm
> Date: 2026-04-10
> Source: [malbiruk/driftwm](https://github.com/malbiruk/driftwm) (Rust + Smithay)

---

## Executive Summary

Driftwm implements a radical **infinite canvas** window management paradigm that fundamentally reimagines how windows exist in desktop space. Unlike traditional tiling or floating window managers, driftwm treats the screen as a **viewport onto an infinite 2D canvas** where windows live as persistent objects.

**Core Philosophy:** Think Figma or Google Maps, but for your desktop. Your screen is a camera that pans and zooms over an unlimited workspace.

**Key Differentiators:**
- **No workspaces** - The infinite canvas replaces workspace abstraction
- **No tiling** - Windows float freely at user-defined positions
- **Trackpad-first** - Designed around modern laptop trackpad gestures
- **Cursor-anchored zoom** - The point under your cursor stays fixed during zoom
- **Multi-monitor as viewports** - Each monitor shows a different view of the same canvas

---

## 1. Infinite Canvas Architecture

### Core Concept

Traditional compositors constrain windows to screen boundaries or organize them into workspaces. Driftwm inverts this model:

```
Traditional Compositor:
┌─────────────────────────────────┐
│  ┌─────┐  ┌─────┐               │  Windows constrained to
│  │  A  │  │  B  │   ┌─────┐     │  screen boundaries
│  └─────┘  └─────┘   │  C  │     │  (may overlap or tile)
│                     └─────┘     │
└─────────────────────────────────┘

Driftwm Infinite Canvas:
              ┌─────┐
              │  E  │
    ┌─────┐   └─────┘        ┌─────┐
    │  A  │                  │  F  │
    └─────┘   ┌──────────┐   └─────┘
              │ Viewport │ ← Screen shows
    ┌─────┐   │  (your   │   portion of
    │  B  │   │  screen) │   infinite canvas
    └─────┘   └──────────┘
              ┌─────┐
    ┌─────┐   │  C  │   ┌─────┐
    │  D  │   └─────┘   │  G  │
    └─────┘             └─────┘

    [Windows scattered across infinite space]
```

### Coordinate System

Driftwm uses a **world coordinate system** where:
- Origin `(0, 0)` is the "home" position (configurable)
- Windows have persistent world coordinates
- The viewport defines which portion of the world is visible
- Zoom scales the viewport's field of view

```rust
// Conceptual driftwm coordinate architecture
pub struct World {
    /// All windows existing in world space
    windows: Vec<Window>,
    /// Current viewport position (world coordinates)
    viewport_origin: Point<f64>,
    /// Current zoom level (1.0 = 100%)
    zoom: f64,
}

pub struct Window {
    /// Persistent position in world coordinates
    world_position: Point<f64>,
    /// Size in logical pixels
    size: Size<f64>,
    /// App ID for window rules
    app_id: String,
    /// Widget flag (pinned to canvas)
    is_widget: bool,
}
```

### Canvas Background

Unlike traditional wallpapers that stick to the screen, driftwm's background is **part of the canvas**:

```
Traditional Wallpaper:          Driftwm Canvas Background:
┌─────────────┐                 ┌─────────────┐
│ ░░░░░░░░░░░ │ Pan right →   │ ░░░░░░░░░░░ │
│ ░ SCREEN ░░ │                 │ ░░SCREEN░░░ │
│ ░░░░░░░░░░░ │                 │ ░░░░░░░░░░░ │
└─────────────┘                 └─────────────┘
(Wallpaper fixed to screen)     (Background scrolls with viewport)
```

**Background Modes:**
1. **GLSL Shaders** - Procedural infinite patterns (default: dot grid)
2. **Tiled Images** - Any PNG/JPG tiled infinitely across canvas

---

## 2. Window Placement

### New Window Placement Strategy

Driftwm places new windows using a **spiral placement algorithm** that finds empty space:

```rust
// Conceptual placement algorithm
fn place_new_window(&mut self, window: Window) -> Point<f64> {
    // Start at viewport center (world coordinates)
    let mut pos = self.viewport_center();
    
    // Search in expanding spiral for empty space
    let mut radius = 100.0;
    loop {
        for angle in [0, 45, 90, 135, 180, 225, 270, 315] {
            let candidate = Point {
                x: pos.x + radius * angle.cos(),
                y: pos.y + radius * angle.sin(),
            };
            if !self.intersects_any_window(candidate, window.size) {
                return candidate;
            }
        }
        radius += 100.0;
    }
}
```

**Placement Rules:**
1. **Viewport-centered** - New windows appear near current viewport center
2. **No overlap** - Algorithm searches for empty space
3. **Window rules override** - Configurable position per app_id/title
4. **Remember position** - Windows maintain world coordinates across sessions

### Window Rules System

Comprehensive per-window configuration via TOML:

```toml
[[window_rules]]
app_id = "Alacritty"
position = [100, 100]
size = [800, 600]
opacity = 0.85
blur = true
decoration = "ssd"  # Server-side decorations

[[window_rules]]
app_id = "my-clock"
position = [50, 50]
widget = true       # Pinned to canvas, below normal windows
decoration = "none" # Borderless
```

**Window Rule Properties:**
| Property | Description |
|----------|-------------|
| `position` | Fixed world coordinates `[x, y]` |
| `size` | Initial window size `[w, h]` |
| `opacity` | Window transparency (0.0 - 1.0) |
| `blur` | Enable background blur |
| `decoration` | `ssd`, `csd`, or `none` |
| `widget` | Pin to canvas, exclude from Alt-Tab |

### Widget System

Widgets are special windows pinned to the canvas:

```
Canvas with widgets:
┌─────────────────────────────────────────┐
│  ┌─────┐                                │
│  │Clock│  ┌─────┐                       │
│  │ 12:00  │Stats│   [Normal windows]    │
│  └─────┘  └─────┘                       │
│                                         │
│      ┌─────────────────────┐            │
│      │                     │            │
│      │    Main Terminal    │            │
│      │                     │            │
│      └─────────────────────┘            │
│                                         │
└─────────────────────────────────────────┘
      ↑ Widgets stay fixed on canvas
```

**Widget Characteristics:**
- Immovable by user interaction
- Rendered below normal windows
- Excluded from Alt-Tab cycling
- Useful for: clocks, system stats, trays, notes

---

## 3. Navigation System

### Pan (Viewport Movement)

Driftwm provides multiple pan methods:

| Input | Action | Context |
|-------|--------|---------|
| 3-finger swipe | Pan viewport | anywhere |
| Trackpad scroll | Pan viewport | on-canvas |
| `Mod` + LMB drag | Pan viewport | anywhere |
| `Mod+Ctrl` + arrow keys | Pan viewport | — |

**Momentum Physics:**
- Quick flick carries viewport with momentum
- Friction gradually slows movement
- Provides natural, tactile feel

```rust
// Conceptual momentum implementation
pub struct Viewport {
    position: Point<f64>,
    velocity: Point<f64>,
    friction: f64,  // Deceleration factor
}

impl Viewport {
    fn update(&mut self, dt: f64) {
        // Apply velocity
        self.position += self.velocity * dt;
        // Apply friction
        self.velocity *= self.friction.powf(dt);
    }
    
    fn apply_impulse(&mut self, impulse: Point<f64>) {
        self.velocity += impulse;
    }
}
```

### Zoom

**Cursor-anchored zoom** keeps the point under your cursor fixed:

```
Zoom at cursor (point B stays fixed):

Before zoom:              After zoom (2x):
┌─────────┐               ┌─────────────────┐
│ A   B   │               │                 │
│    ↑    │     zoom      │       A         │
│ C   D   │    ─────→     │          B      │
└─────────┘   at cursor   │            C    │
                            │               D │
                            └─────────────────┘
                        B remains at same screen position
```

**Zoom Methods:**
| Input | Action |
|-------|--------|
| 2-finger pinch | Zoom at cursor |
| 3-finger pinch | Zoom at cursor (anywhere) |
| `Mod` + scroll | Zoom at cursor |
| `Mod+=` / `Mod+-` | Zoom in / out |
| `Mod+0` / `Mod+Z` | Reset zoom to 1.0 |

### Directional Window Navigation (Cone Search)

Driftwm uses **cone search** to find the nearest window in a direction:

```
Cone search visualization:
                    ∧
                   / \   60° cone
                  /   \   for "up"
                 /     \
        ┌───────┐       ┌───────┐
        │   A   │       │   B   │
        └───────┘       └───────┘
        
              ┌───────────┐
              │ FOCUSED   │
              └───────────┘

Window A is selected (closest in cone)
```

**Navigation Inputs:**
| Input | Action |
|-------|--------|
| 4-finger swipe | Jump to nearest window (natural direction) |
| `Mod+Ctrl` + LMB drag | Jump to nearest window |
| `Mod` + arrow | Jump to nearest window in direction |
| `Alt-Tab` | Cycle windows (MRU) |

### Special Navigation Modes

**Zoom-to-Fit (Overview):**
- Scales view to show all windows
- Like Niri's overview but for infinite canvas
- 4-finger pinch in or `Mod+W`

**Home Toggle:**
- Jump to origin (0, 0) or return to previous position
- 4-finger pinch out or `Mod+A`

**Center Focused:**
- Centers viewport on focused window
- 4-finger hold or `Mod+C`

**Bookmarks:**
- Save canvas positions with `Mod+Shift+1-4`
- Jump to bookmarks with `Mod+1-4`

---

## 4. Focus System

### Focus Model

Driftwm supports two focus modes:

1. **Click-to-focus** (default) - Click window to focus
2. **Focus-follows-mouse** (sloppy focus) - Focus window under cursor

### Focus Ring & Visual Feedback

```
Window decorations:
┌─────────────────────────┐
│ ┌─────────────────────┐ │
│ │                     │ │
│ │   Window Content    │ │
│ │                     │ │
│ └─────────────────────┘ │
│      ↑ Focus ring       │
│    (colored border)     │
│       + Shadow          │
└─────────────────────────┘
```

**Consistent decorations** across CSD and SSD windows:
- Rounded corners (configurable radius)
- Drop shadows for depth perception
- Focus ring color indicates active window

### Window Stacking Order

```
Z-order (bottom to top):
1. Canvas background (GLSL shader or tiled image)
2. Layer-shell background/bottom layers
3. Widget windows (pinned, immovable)
4. Normal windows (ordered by focus history)
5. Layer-shell top/overlay layers
6. Focused window (optional: bring to front)
```

---

## 5. Rust + Smithay Implementation

### Architecture Stack

```
┌─────────────────────────────────────────────────────────────┐
│                    Driftwm Compositor                        │
│                       (Rust)                                │
├─────────────────────────────────────────────────────────────┤
│  Smithay Toolkit                                            │
│  ├── Wayland Protocol Handlers                              │
│  ├── Input Event Processing                                 │
│  ├── Rendering Abstractions                                 │
│  └── Output Management                                      │
├─────────────────────────────────────────────────────────────┤
│  wlroots (via Smithay bindings)                            │
│  ├── wlr_scene (scene graph)                               │
│  ├── wlr_output (display outputs)                          │
│  ├── wlr_seat (input focus)                                │
│  └── 30+ Wayland protocols                                 │
├─────────────────────────────────────────────────────────────┤
│  System Libraries                                           │
│  ├── libwayland (Wayland core)                             │
│  ├── libinput (input devices)                              │
│  ├── GBM/DMA-BUF (GPU buffers)                             │
│  └── OpenGL/Vulkan (rendering)                             │
└─────────────────────────────────────────────────────────────┘
```

**Note:** Driftwm is written in **Rust**, not Python as mentioned in some sources. It uses the **Smithay** Rust compositor toolkit, which provides Rust bindings to wlroots concepts.

### Key Dependencies

```toml
# From Cargo.toml (conceptual)
[dependencies]
smithay = "0.5"           # Wayland compositor toolkit
calloop = "0.14"          # Event loop
wayland-server = "0.31"   # Wayland server protocol
wayland-protocols = "0.31" # Standard protocols
wayland-backend = "0.3"   # Backend abstraction
glam = "0.29"             # Linear algebra (transforms)
pixman = "0.2"            # Software rendering fallback
```

### Rendering Pipeline

```rust
// Conceptual driftwm rendering
pub struct Renderer {
    scene: wlr_scene::Scene,
    viewport: Viewport,
    zoom: f64,
}

impl Renderer {
    fn render(&mut self, output: &Output) {
        // 1. Apply viewport transform (pan + zoom)
        let transform = Matrix4::identity()
            .translate(-self.viewport.origin.x, -self.viewport.origin.y, 0.0)
            .scale(self.zoom, self.zoom, 1.0);
        
        // 2. Render canvas background (GLSL shader)
        self.render_background(output, transform);
        
        // 3. Render visible windows
        for window in self.visible_windows() {
            window.render(output, transform);
        }
        
        // 4. Render layer-shell surfaces (fixed to screen)
        self.render_layers(output);
    }
}
```

### World-to-Screen Coordinate Transform

```rust
/// Convert world coordinates to screen coordinates
fn world_to_screen(&self, world_pos: Point<f64>) -> Point<f64> {
    Point {
        x: (world_pos.x - self.viewport.origin.x) * self.zoom,
        y: (world_pos.y - self.viewport.origin.y) * self.zoom,
    }
}

/// Convert screen coordinates to world coordinates
fn screen_to_world(&self, screen_pos: Point<f64>) -> Point<f64> {
    Point {
        x: screen_pos.x / self.zoom + self.viewport.origin.x,
        y: screen_pos.y / self.zoom + self.viewport.origin.y,
    }
}

/// Check if window is visible in viewport
fn is_visible(&self, window: &Window) -> bool {
    let screen_rect = self.viewport.to_screen_rect();
    let window_rect = Rect {
        x: window.position.x,
        y: window.position.y,
        w: window.size.w,
        h: window.size.h,
    };
    screen_rect.intersects(&window_rect)
}
```

---

## 6. Multi-Monitor Architecture

### Multiple Viewports Model

Driftwm treats each monitor as an **independent viewport** on the same canvas:

```
Same canvas, two monitors:

┌─────────────────────────────────────────────────────────────┐
│                        Infinite Canvas                       │
│                                                             │
│   ┌─────┐                   ┌─────────┐    ┌─────┐         │
│   │  A  │                   │    B    │    │  C  │         │
│   └─────┘                   └─────────┘    └─────┘         │
│                                                             │
│   ┌─────────────────┐                                       │
│   │                 │      ┌─────────────────────┐          │
│   │    Monitor 1    │      │                     │          │
│   │    (Viewport)   │      │     Monitor 2       │          │
│   │    [Screen]     │      │     (Viewport)      │          │
│   │                 │      │     [Screen]        │          │
│   └─────────────────┘      │                     │          │
│                            └─────────────────────┘          │
│                                                             │
│   ┌─────┐                                                   │
│   │  D  │                                                   │
│   └─────┘                                                   │
│                                                             │
└─────────────────────────────────────────────────────────────┘

- Monitors can overlap (show same area)
- Monitors can have different zoom levels
- Outline shows other monitors' viewports
```

### Monitor-to-Monitor Interaction

**Cursor Crossing:**
- Cursor moves freely between monitors
- Drags window to another monitor → window teleports to that viewport's canvas position

**Window Sending:**
- `Mod+Alt+arrow` sends window to adjacent output
- Window keeps world coordinates but viewport changes

**Viewport Outline:**
- Each monitor shows an outline of other monitors' viewports
- Provides spatial awareness on the infinite canvas

---

## 7. Comparison with Niri and Traditional Compositors

### Driftwm vs Niri

| Aspect | Driftwm | Niri |
|--------|---------|------|
| **Paradigm** | Infinite floating canvas | Scrollable-tiling columns |
| **Window placement** | Free positioning | Structured columns |
| **Navigation** | Pan + zoom viewport | Scroll horizontally |
| **Workspaces** | None (replaced by canvas) | Dynamic workspaces |
| **New windows** | Spiral search placement | Add to right of active |
| **Zoom** | Continuous (0.1x - 5x+) | Fixed overview mode |
| **Background** | Canvas-scrolling GLSL shader | Fixed per-workspace |
| **Window rules** | Position, widget, opacity | Width, floating, output |
| **Focus navigation** | Cone search (8 directions) | Column left/right |

### Architecture Comparison

```
Driftwm:                         Niri:
┌──────────────────┐            ┌──────────────────┐
│   Viewport       │            │   Workspace      │
│  (position +     │            │  (scrolling +    │
│   zoom level)    │            │   floating)      │
└────────┬─────────┘            └────────┬─────────┘
         │                               │
         ▼                               ▼
┌──────────────────┐            ┌──────────────────┐
│  Window List     │            │  Column List     │
│  (world coords)  │            │  (scrollable)    │
└────────┬─────────┘            └────────┬─────────┘
         │                               │
         ▼                               ▼
┌──────────────────┐            ┌──────────────────┐
│  Canvas Renderer │            │  Layout Engine   │
│  (transform      │            │  (tile sizing)   │
│   based)         │            │                  │
└──────────────────┘            └──────────────────┘
```

### Feature Matrix

| Feature | Driftwm | Niri | Traditional Tiling (Sway) | Floating (Openbox) |
|---------|---------|------|---------------------------|-------------------|
| Infinite canvas | Yes | Horizontal only | No | No |
| Free window position | Yes | Warning: Within columns | No | Yes |
| Continuous zoom | Yes | Overview only | No | No |
| Tiling | No | Yes | Yes | No |
| Workspaces | No | Yes Dynamic | Yes Fixed | Yes |
| Trackpad gestures | Yes First-class | Yes Good | Basic | No |
| Window rules | Yes Extensive | Yes Good | Yes Basic | Warning: Limited |
| Widgets | Yes | No | No | External |
| Multi-monitor viewports | Yes Independent | Warning: Shared | Warning: Shared | No Separate X screens |

### Use Case Fit

**Driftwm is ideal for:**
- Laptop users with limited screen real estate
- Creative work requiring spatial organization
- Users who prefer trackpad gestures
- People who think in "maps" rather than "lists"
- Widget-heavy setups (clocks, stats, notes pinned to canvas)

**Niri is ideal for:**
- Developer workflows with many terminal windows
- Users who want predictable window placement
- Keyboard-centric navigation
- Users who prefer structured layouts

**Traditional tiling is ideal for:**
- Maximum screen utilization
- Keyboard-only operation
- Simple, predictable behavior

---

## 8. Unique Features

### 1. Cursor-Anchored Zoom

Unlike zoom systems that zoom toward screen center, driftwm zooms toward the cursor:

```python
# Conceptual cursor-anchored zoom
fn zoom_at_cursor(&mut self, cursor_world: Point<f64>, delta: f64) {
    let old_zoom = self.zoom;
    self.zoom = (self.zoom * delta).clamp(0.1, 5.0);
    
    // Adjust viewport to keep cursor at same screen position
    let zoom_ratio = self.zoom / old_zoom;
    self.viewport.origin.x = cursor_world.x - (cursor_world.x - self.viewport.origin.x) / zoom_ratio;
    self.viewport.origin.y = cursor_world.y - (cursor_world.y - self.viewport.origin.y) / zoom_ratio;
}
```

This matches user expectations from:
- Google Maps pinch-to-zoom
- Figma canvas navigation
- Image editors

### 2. Infinite GLSL Background

The canvas background is a **procedural GLSL shader**:

```glsl
// Default dot grid shader (conceptual)
uniform vec2 u_viewport_origin;
uniform float u_zoom;

void main() {
    vec2 world_pos = (gl_FragCoord.xy / u_zoom) + u_viewport_origin;
    
    // Create infinite dot grid
    vec2 grid = fract(world_pos / 100.0);
    float dot = smoothstep(0.02, 0.03, length(grid - 0.5));
    
    gl_FragColor = vec4(vec3(dot), 1.0);
}
```

**Benefits:**
- No texture memory usage
- Infinite detail at any zoom
- Consistent visual pattern
- GPU-accelerated

### 3. Magnetic Snapping

Window drag operations include **magnetic edge snapping**:

```
Magnetic snap visualization:

┌─────────┐    ┌─────────┐
│         │    │         │
│    A    │◄──►│    B    │  ← Snap distance threshold
│         │    │         │     (20px default)
└─────────┘    └─────────┘
     
     ↑ Edges align when within threshold
```

### 4. Edge Auto-Pan

Dragging a window to viewport edge **auto-pans** the canvas:

```
Auto-pan during drag:
┌─────────────────┐
│                 │
│   ┌────┐        │  Drag window to edge
│   │Window       │  → Canvas pans
│   │→    │        │     → More space revealed
│   └────┘        │
│              ███│  ███ = Edge trigger zone
└─────────────────┘
```

### 5. Cone Search Algorithm

Directional navigation uses sophisticated cone search:

```rust
// Conceptual cone search
fn find_nearest_in_direction(
    &self,
    from: Point<f64>,
    direction: Direction,
    windows: &[Window],
) -> Option<&Window> {
    let cone_angle = 60f64.to_radians();  // 60 degree cone
    let direction_vec = direction.to_vector();
    
    windows.iter()
        .filter(|w| {
            let to_window = w.center() - from;
            let angle = direction_vec.angle_to(to_window);
            angle.abs() < cone_angle / 2.0
        })
        .min_by_key(|w| distance(from, w.center()))
}
```

---

## 9. Key Insights for kalin-wm Implementation

### Architecture Patterns to Adopt

#### 1. World Coordinate System

```rust
// Pattern: Separate world and screen coordinates
pub struct World {
    windows: HashMap<WindowId, Window>,
    viewport: Viewport,
}

pub struct Viewport {
    origin: Point<f64>,  // World position of screen center (or corner)
    zoom: f64,
}
```

#### 2. Transform-Based Rendering

```rust
// Pattern: Apply zoom/pan via transform matrix
pub fn render(&self, renderer: &mut Renderer) {
    let transform = Matrix4::identity()
        .translate(-self.viewport.origin.x, -self.viewport.origin.y, 0.0)
        .scale(self.viewport.zoom, self.viewport.zoom, 1.0);
    
    for window in &self.windows {
        window.render(renderer, transform);
    }
}
```

#### 3. Input Coordinate Transformation

```rust
// Pattern: Transform all input to world coordinates
pub fn handle_pointer_motion(&mut self, screen_pos: Point<f64>) {
    let world_pos = self.screen_to_world(screen_pos);
    
    // Hit test in world space
    if let Some(window) = self.window_at(world_pos) {
        window.handle_motion(world_pos - window.position);
    }
}
```

### Unique Opportunities for kalin-wm

1. **Hybrid Approach**: Combine driftwm's infinite canvas with Niri's scrollable columns
   - Columns scroll within the infinite canvas
   - Best of both paradigms

2. **Resolution-Aware Zoom**: 
   - Driftwm doesn't mention fractional scaling handling
   - kalin-wm could implement crisp text at all zoom levels
   - See [[fractional-scaling|Fractional Scaling]] research

3. **Scene Graph Integration**:
   - Use [[definitions#wlr_scene|wlr_scene]] for efficient rendering
   - Automatic damage tracking during pan/zoom
   - See [[scene-graph-scaling|Scene Graph Scaling]]

4. **Buffer Scaling Considerations**:
   - Current driftwm likely scales buffers (potential blur)
   - kalin-wm could implement [[definitions#Resolution Independence|resolution-independent rendering]]
   - See [[scaling-algorithms|Scaling Algorithms]] for quality options

### Implementation Warnings

1. **Input Latency**: World coordinate transforms must be applied to all input events
2. **Damage Tracking**: Pan operations require full-screen redraws without proper scene graph
3. **Memory Usage**: Infinite canvas means windows far from viewport still consume resources
4. **Focus Management**: Click-to-focus requires hit-testing in world coordinates
5. **XWayland**: X11 apps may not handle arbitrary window positions well

### Configuration Ideas for kalin-wm

```toml
# Potential kalin-wm config (inspired by driftwm)
[canvas]
background_shader = "dot_grid.glsl"
home_position = [0, 0]
zoom_limits = [0.1, 5.0]
momentum_friction = 0.9

[navigation]
cone_search_angle = 60  # degrees
snap_distance = 20      # pixels
auto_pan_speed = 500    # pixels/second

[[window_rules]]
app_id = "terminal"
position = "spiral"     # Or "center", "random", [x, y]
size = [800, 600]
```

---

## References

- [Driftwm GitHub Repository](https://github.com/malbiruk/driftwm)
- [Driftwm Design Document](https://github.com/malbiruk/driftwm/blob/main/docs/DESIGN.md)
- [Smithay Documentation](https://smithay.github.io/)
- [Niri Scrollable-Tiling](https://github.com/YaLTeR/niri)
- [[niri-layout|Niri Layout Analysis]]
- [[zoom-features|Compositor Zoom Features]]
- [[definitions|Technical Definitions Glossary]]
- [[scene-graph-scaling|Scene Graph API]]
