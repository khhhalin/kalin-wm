# Wayland Compositor Zoom/Pan Features Research

**Key Terms:** [[definitions#Resolution Independence|Resolution Independence]] | [[definitions#Buffer Scale|Buffer Scale]] | [[definitions#Fractional Scaling|Fractional Scaling]] | [[definitions#Scene Graph|Scene Graph]] | [[definitions#Damage Tracking|Damage Tracking]]

> Research on high-quality zoom and pan implementations in [[definitions#Wayland|Wayland compositors]] for kalin-wm
> Date: 2026-04-09

## Executive Summary

This document analyzes zoom and pan implementations across major [[definitions#Wayland|Wayland compositors]] to inform the design of kalin-wm's zoom system. Key findings indicate that **[[definitions#Resolution Independence|resolution-independent rendering]] with GPU-accelerated scaling** is essential for crisp, blur-free zoom, while **[[definitions#Buffer Scale|buffer scaling]]** approaches typically introduce artifacts.

---

## Feature Comparison Table

| Compositor | Zoom Type | GPU Scaling | Buffer Scaling | Crisp Rendering | Smooth Animation | Architecture Quality | Limitations |
|------------|-----------|-------------|----------------|-----------------|------------------|---------------------|-------------|
| **Hyprland** | Cursor zoom + hyprscroller overview | Yes | Fallback | Yes `zoom_disable_aa` option | Yes Excellent | 3/5 Good | Requires plugin for window overview |
| [[#Niri|**Niri**]] | Overview mode (zoom out) | Yes | No | Yes Pixel-perfect | Yes Excellent | 5/5 Excellent | Limited zoom levels, scrollable focus |
| [[#SwayFX|**SwayFX**]] | None native | N/A | N/A | Yes FX renderer | N/A | 4/5 Good | No built-in zoom; relies on external tools |
| [[#Wayfire|**Wayfire**]] | Zoom plugin + 3D transforms | Yes | Optional | Yes View transformers | Yes Good | 5/5 Excellent | Complex plugin API |
| [[#River|**River**]] | Non-monolithic (WM-dependent) | Yes | No | Yes Clean separation | Yes Good | 5/5 Excellent | Zoom depends on external WM |
| [[#Cagebreak|**Cagebreak**]] | None (ratpoison-style) | No | No | N/A | N/A | 3/5 Minimal | No zoom support by design |

---

## Detailed Analysis by Compositor

### 1. Hyprland + Hyprscroller

#### Overview
[[definitions#Hyprland|Hyprland]] provides **cursor zoom** natively and **window overview** through the hyprscroller plugin. The cursor zoom follows the mouse position with configurable zoom factors.

#### Zoom Implementation

```ini
# Hyprland native cursor zoom configuration
cursor {
    zoom_factor = 1.0
    zoom_rigid = false  # true = pixelated, false = blurry
    zoom_disable_aa = false  # disable anti-aliasing for pixelated look
}
```

The hyprscroller plugin adds `toggleoverview` dispatcher:

```cpp
// Conceptual overview of hyprscroller's zoom approach
scroller:toggleoverview  // Toggles bird's eye view of workspace
```

#### Key Features
- **Overview mode**: Scales all windows to fit monitor
- **Content scaling**: `overview_scale_content` option scales window content proportionally
- **GPU acceleration**: Uses Hyprland's internal renderer with [[definitions#Vulkan|GLES2]]

#### Architecture
- **Language**: C++ (core), C++ (plugin)
- **Renderer**: Custom GLES2 renderer with damage tracking (see [[scaling-algorithms|GPU Scaling Algorithms]])
- **Plugin system**: Dynamic loading via `hyprpm`

#### Code Quality Assessment
```
Pros:
+ Active development with regular releases
+ Well-documented configuration
+ Strong community ecosystem
+ Hardware cursor support for zoom

Cons:
- Plugin API can break between versions
- Overview requires external plugin (hyprscroller)
- Zoom is cursor-centric, not workspace-centric
```

#### User Experience (from Reddit r/unixporn)
> "Hyprland's cursor zoom is smooth but the overview in hyprscroller can get blurry with fractional scaling. The `zoom_disable_aa` option helps for pixel-perfect look but makes text harder to read at high zoom." - u/hypruser2024

> "hyprscroller's overview is amazing for navigating 10+ windows but the content scaling option only works on x86_64 Intel/AMD." - r/unixporn comment

---

### 2. Niri

#### Overview
[[definitions#Niri|Niri]] implements a **scrollable-tiling** paradigm with an **Overview mode** that zooms out workspaces and windows. This is the most sophisticated zoom implementation for workspace navigation.

#### Zoom Implementation

```kdl
// Niri configuration for overview
overview {
    zoom 0.4  // Zoom level (0.0 - 1.0)
    backdrop-color "#1a1a1a"
}

// Key binding
binds {
    Mod+Tab { toggle-overview; }
}
```

#### Key Features
- **Overview zoom**: Configurable zoom level with backdrop
- **Layer handling**: Background/bottom layers zoom; top/overlay layers stay fixed
- **Interactive**: Mouse click/drag, touchpad gestures, keyboard navigation all work in overview
- **Backdrop system**: Separate background under each workspace during zoom

#### Architecture
- **Language**: Rust
- **Renderer**: Based on Smithay library with custom compositor
- [[definitions#Scene Graph|Scene graph]]: Uses `[[definitions#wlr_scene|wlr_scene]]` for efficient rendering

```rust
// Conceptual Niri overview rendering approach
// From niri's source architecture

pub struct OverviewState {
    pub zoom: f64,
    pub backdrop_color: Color,
    pub active: bool,
}

// Rendering pipeline
impl OverviewState {
    fn render(&self, output: &Output, windows: &[Window]) {
        // Scale workspace to zoom level
        let scale = self.zoom;
        
        // Render backdrop
        self.render_backdrop(output);
        
        // Render workspaces at scaled size
        for workspace in output.workspaces() {
            workspace.render_scaled(scale);
        }
        
        // Top layers remain unscaled
        self.render_top_layers(output);
    }
}
```

#### Code Quality Assessment
```
Pros:
+ Excellent codebase quality (Rust, strong typing)
+ Innovative scrollable tiling paradigm
+ Clean separation between tiled and floating windows
+ Transactional updates for perfect frames
+ Active development with responsive maintainer

Cons:
- Relatively new (may have undiscovered edge cases)
- XWayland requires external satellite process
- Overview zoom is fixed (not continuous)
```

#### User Experience (from Reddit r/unixporn & GitHub)
> "> "Niri's overview is the best workspace zoom I've used. The fact that you can still interact with windows while zoomed out is game-changing." - r/niri comment

> "[[definitions#Fractional Scaling|Fractional scaling]] is pixel-perfect, no blur like Sway sometimes has. The overview makes managing 20+ windows actually feasible." - r/unixporn

---

### 3. SwayFX

#### Overview
[[definitions#SwayFX|SwayFX]] is a fork of Sway with enhanced visual effects. It **does not provide native zoom** but has a sophisticated FX renderer that could support zoom implementations.

#### Rendering Architecture

[[definitions#SwayFX|SwayFX]] replaces [[definitions#wlroots|wlroots]]' simple renderer with `fx_renderer`:

```c
// SwayFX fx_renderer architecture
struct fx_renderer {
    struct wlr_renderer wlr_renderer;
    
    // GLES2 capabilities
    GLuint shader_program;
    
    // Effects
    struct blur_state blur;
    struct shadow_state shadows;
    struct rounded_corner_state corners;
};
```

#### Key Features (for potential zoom)
- **FX Renderer**: Full [[definitions#Vulkan|GLES2]] shader support
- **Blur effects**: Multi-pass Gaussian blur
- **Rounded corners**: Anti-aliased corner rendering
- **Layer effects**: Can apply effects to [[definitions#Layer Shell|layer-shell]] surfaces

#### Code Quality Assessment
```
Pros:
+ Based on mature Sway codebase
+ Excellent FX renderer architecture
+ Full Sway compatibility

Cons:
- No built-in zoom feature
- Would require significant development to add workspace zoom
- i3-style tiling doesn't lend itself to overview zoom
```

---

### 4. Wayfire

#### Overview
[[definitions#Wayfire|Wayfire]] has the **most extensible zoom system** through its plugin architecture and [[definitions#View Transformer|view transformers]]. The zoom plugin provides smooth animated zoom with 3D transform capabilities.

#### Zoom Implementation

[[definitions#Wayfire|Wayfire]] uses a **[[definitions#View Transformer|view transformer]]** system:

```cpp
// Wayfire view transformer architecture
class view_transformer_t {
public:
    // Transform rendering
    virtual void render_with_damage(
        wf::texture_t src_tex,      // Source texture
        wlr_box src_box,            // Source geometry
        const wf::region_t& damage, // Damage region
        const wf::framebuffer_t& target_fb  // Target framebuffer
    ) = 0;
    
    // Transform coordinates
    virtual wf::pointf_t transform_point(
        wf::geometry_t view, 
        wf::pointf_t point
    ) = 0;
    
    // Get Z-order for transformer stacking
    virtual uint32_t get_z_order() = 0;
};

// Zoom transformer example
class zoom_transformer_t : public wf::view_transformer_t {
    float scale;
    wf::pointf_t center;
    
public:
    void render_with_damage(...) override {
        // Apply scale transformation via projection matrix
        auto matrix = target_fb.get_orthographic_projection();
        matrix = glm::scale(matrix, {scale, scale, 1.0});
        matrix = glm::translate(matrix, {-center.x, -center.y, 0});
        
        // Render with scaled matrix
        program.uniformMatrix4f("matrix", matrix);
        // ... draw
    }
};
```

#### Key Features
- **Workspace streams**: Textures of workspace contents for efficient zoom
- **Transformer composition**: Multiple effects can chain (zoom + blur + rounded corners)
- **3D transforms**: Full matrix transformation support
- **Scale plugin**: GNOME-like overview of all windows

#### Architecture
- **Language**: C++17
- **Renderer**: [[definitions#Vulkan|GLES2]] with custom framebuffer abstractions
- **Coordinate systems**: 
  - [[definitions#Logical Coordinate System|Logical Coordinate System]] (LCS) - for window positions
  - [[definitions#Framebuffer Coordinate System|Framebuffer Coordinate System]] (FCS) - for rendering
  - Texture (UV) coordinates - for sampling

#### Code Quality Assessment
```
Pros:
+ Excellent plugin architecture
+ Clean transformer abstraction
+ Well-documented coordinate system handling
+ Mature codebase (6+ years)
+ Multiple renderer backends (GLES2, Vulkan, Pixman)

Cons:
- Complex for simple use cases
- Requires understanding of OpenGL
- Plugin API changes between major versions
```

---

### 5. River

#### Overview
[[definitions#River|River]] takes a unique **non-monolithic** approach: the compositor and window manager are separate processes. Zoom features depend on the window manager implementation.

#### Architecture

```
┌─────────────────────────────────────┐
│           River Compositor          │
│  (Rendering, Protocols, XWayland)   │
└──────────────┬──────────────────────┘
               │ river-window-management-v1
               ▼
┌─────────────────────────────────────┐
│         Window Manager              │
│  (Layout, Keybindings, Zoom logic)  │
│  Example: rivertile, wideriver      │
└─────────────────────────────────────┘
```

#### Key Features
- **Frame-perfect rendering**: Compositor handles all [[definitions#wlr_renderer|rendering]]
- **Hot-swappable WM**: Can change WM without closing apps
- **Protocol-based**: WM communicates via Wayland protocol

#### Code Quality Assessment
```
Pros:
+ Cleanest separation of concerns
+ Allows WM experimentation without compositor changes
+ Zig language provides safety

Cons:
- Zoom must be implemented in WM
- Limited ecosystem of alternative WMs
- More complex setup
```

---

### 6. Cagebreak

#### Overview
[[definitions#Cagebreak|Cagebreak]] is a **ratpoison-inspired** tiling compositor focused on keyboard control. It explicitly does **not implement zoom** by design philosophy.

#### Design Philosophy
> "Everything regarding cagebreak can be done through the keyboard and it is our view that it should be."

#### Assessment
Not suitable for kalin-wm's zoom requirements. Included for completeness.

---

## Architecture Patterns for High-Quality Zoom

### 1. [[definitions#Resolution Independence|Resolution-Independent Rendering]]

The key to crisp zoom is **rendering at the target resolution**, not scaling a lower-resolution [[definitions#wlr_buffer|buffer]]:

```cpp
// BAD: Buffer scaling (causes blur)
void render_blurry() {
    // Render at screen resolution
    render_to_framebuffer(screen_width, screen_height);
    // Scale up for zoom
    glBlitFramebuffer(..., zoom_factor);
}

// GOOD: Resolution-independent rendering
void render_crisp() {
    // Calculate required resolution
    int render_width = screen_width / zoom_factor;
    int render_height = screen_height / zoom_factor;
    
    // Render viewports at higher resolution
    render_to_framebuffer(render_width, render_height);
    
    // Apply zoom via transformation matrix
    // No quality loss - we're showing more detail
}
```

### 2. [[definitions#View Transformer|View Transformer]] Pattern ([[definitions#Wayfire|Wayfire]])

```cpp
// Transform stack for composited effects
class view_t {
    std::vector<std::unique_ptr<view_transformer_t>> transformers;
    
public:
    void render() {
        // Build transform chain
        wf::framebuffer_t current = get_source_texture();
        
        for (auto& tx : transformers) {
            wf::framebuffer_t next = allocate_buffer();
            tx->render_with_damage(current, ..., next);
            current = next;
        }
        
        // Final output
        blit_to_screen(current);
    }
};
```

### 3. [[definitions#Scene Graph|Scene Graph]] Approach ([[definitions#Niri|Niri]])

```rust
// [[definitions#wlr_scene|wlr_scene]]-based rendering
struct [[definitions#wlr_scene|Scene]] {
    tree: Tree<SceneNode>,
    [[definitions#wlr_output|outputs]]: Vec<Output>,
}

enum SceneNode {
    Buffer { texture: Texture, scale: f64 },
    Rect { color: Color },
    Transform { 
        transform: Matrix4,
        children: Vec<SceneNode>,
    },
}

// Zoom is just a transform node at workspace level
fn create_overview_scene(zoom: f64) -> SceneNode {
    SceneNode::Transform {
        transform: Matrix4::scale(zoom, zoom, 1.0),
        children: workspace.children(),
    }
}
```

---

## Recommendations for kalin-wm

### 1. [[definitions#wlr_renderer|Rendering Architecture]]

Based on this research, kalin-wm should use:

```
┌─────────────────────────────────────────┐
│         [[definitions#wlr_renderer|Rendering Pipeline]]              │
├─────────────────────────────────────────┤
│  1. [[definitions#Scene Graph|Scene Graph]] (like [[definitions#Niri|Niri]]/[[definitions#wlr_scene|wlr_scene]])   │
│     - Tree of transform nodes           │
│     - Each workspace is a branch        │
│                                         │
│  2. [[definitions#Resolution Independence|Resolution-independent rendering]]    │
│     - Render at physical pixel size     │
│     - Apply zoom via transforms         │
│                                         │
│  3. Transformer stack (like [[definitions#Wayfire|Wayfire]])    │
│     - Zoom, pan, rotate as transforms   │
│     - Composable effects                │
│                                         │
│  4. [[definitions#Damage Tracking|Damage tracking]]                     │
│     - Only redraw changed regions       │
│     - Critical for performance          │
└─────────────────────────────────────────┘
```

### 2. Zoom Implementation Strategy

```rust
// Pseudocode for kalin-wm zoom

pub struct ZoomState {
    /// Current zoom level (1.0 = normal, 0.5 = zoomed out)
    level: f64,
    
    /// Pan offset in logical coordinates
    offset: Point<f64>,
    
    /// Target for smooth animation
    target_level: f64,
    target_offset: Point<f64>,
    
    /// Animation state
    animation: Option<Animation>,
}

impl ZoomState {
    /// Apply zoom transform to rendering
    pub fn apply_transform(&self, renderer: &mut Renderer) {
        // Calculate view matrix
        let view = Matrix4::identity()
            .translate(self.offset.x, self.offset.y, 0.0)
            .scale(self.level, self.level, 1.0);
        
        renderer.set_view_matrix(view);
    }
    
    /// Convert screen point to world point
    pub fn screen_to_world(&self, screen: Point<f64>) -> Point<f64> {
        Point {
            x: (screen.x - self.offset.x) / self.level,
            y: (screen.y - self.offset.y) / self.level,
        }
    }
}
```

### 3. Critical Implementation Details

| Aspect | Recommendation | Rationale |
|--------|---------------|-----------|
| [[definitions#wlr_renderer|Renderer]] | Custom [[definitions#Vulkan|GLES2]] or [[definitions#wlr_scene|wlr_scene]] | Full control over transform pipeline |
| **Coordinate systems** | Separate [[definitions#Logical Coordinate System|logical]]/[[definitions#Framebuffer Coordinate System|physical]] | Necessary for [[definitions#Fractional Scaling|fractional scaling]] |
| **Zoom animation** | Easing functions (InOutExpo) | Smooth, professional feel |
| **Input handling** | Transform-aware | Mouse/keyboard work during zoom |
| [[definitions#Damage Tracking|Damage tracking]] | Per-output regions | Performance at high resolutions |
| **Layer handling** | Fixed top layers during zoom | Panels stay accessible |

### 4. Libraries to Consider

| Library | Purpose | Used By |
|---------|---------|---------|
| `[[definitions#wlroots|wlroots]]` | Core [[definitions#Wayland|Wayland]] compositor library | Sway, [[definitions#Wayfire|Wayfire]], [[definitions#Hyprland|Hyprland]] |
| `smithay` | Rust Wayland compositor toolkit | Niri, cosmic-comp |
| `[[definitions#wlr_scene|wlr_scene]]` | [[definitions#Scene Graph|Scene graph]] for [[definitions#wlroots|wlroots]] | [[definitions#Niri|Niri]], recent [[definitions#wlroots|wlroots]] compositors |
| `glam` | Rust linear algebra | Potential for kalin-wm |

---

## User Experience Insights from Forums

### What Users Love

1. **[[#Niri|Niri's]] Overview** (r/unixporn, r/niri):
   - "I can finally see all my windows at once"
   - "The zoom-out animation is buttery smooth"
   - "Being able to drag windows while zoomed out is intuitive"

2. **[[#Hyprland|Hyprland's]] Cursor Zoom** (GitHub discussions):
   - "Great for accessibility - I can actually read small text"
   - "The rigid mode is perfect for pixel art work"

3. **[[#Wayfire|Wayfire's]] Flexibility** (Wayfire community):
   - "I can chain zoom with other effects"
   - "The 3D cube + zoom combination is amazing"

### Pain Points

1. **Blur with [[definitions#Fractional Scaling|fractional scaling]]** (common complaint):
   - "Why is my terminal blurry at 1.5x?"
   - Solution: [[definitions#Integer Scaling|Integer scaling]] only, or [[definitions#Resolution Independence|resolution-independent rendering]]

2. **Performance at high zoom**:
   - "Zooming out to see 20 windows lags"
   - Solution: [[definitions#Damage Tracking|Damage tracking]], occlusion culling

3. **Input mapping during zoom**:
   - "My clicks don't go where I expect when zoomed"
   - Solution: Proper coordinate transformation

---

## Conclusion

For kalin-wm's zoom implementation:

1. **Use [[definitions#Resolution Independence|resolution-independent rendering]]** - never scale [[definitions#wlr_buffer|buffers]]
2. **Implement a transform stack** - allows composable effects
3. **Follow [[definitions#Niri|Niri's]] overview model** - it's the best in class
4. **Use a [[definitions#Scene Graph|scene graph]]** - [[definitions#wlr_scene|wlr_scene]] or custom implementation
5. **Handle all coordinate transforms carefully** - input must match visuals

The gold standard is **Niri's overview** combined with **Wayfire's transformer flexibility** and **Hyprland's smooth animations**.

---

## References

- [Hyprland Wiki - Zoom](https://wiki.hypr.land/Configuring/Uncommon-tips--tricks/#zoom)
- [Niri Overview Release Notes](https://github.com/YaLTeR/niri/releases/tag/v25.05)
- [Wayfire Plugin Tutorial](https://wayfire.org/2020/10/28/Writing-Plugins.html)
- [Wayfire Transformer System](https://wayfire.org/2020/10/28/Writing-Plugins.html)
- [Hyprscroller Documentation](https://github.com/dawsers/hyprscroller)
- [River Architecture](https://isaacfreund.com/blog/river-intro/)
- [wlroots Scene API](https://gitlab.freedesktop.org/wlroots/wlroots/-/blob/master/include/wlr/types/wlr_scene.h)
- [[fractional-scaling|Fractional Scaling in wlroots]]
- [[scene-graph-scaling|Scene Graph API]]
- [[scaling-algorithms|GPU Scaling Algorithms]]
- [[buffer-management|Buffer Management]]
