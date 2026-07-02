# Wayland Protocols Checklist for kalin-wm

> Complete reference of required, recommended, and optional Wayland protocols for full application compatibility.

> **Canonical status source:** [protocols/protocol-matrix.md](protocols/protocol-matrix.md).  
> This checklist is a detailed companion document.

## Quick Status

| Category | Implemented | Missing | Total |
|----------|-------------|---------|-------|
| Essential | 15 | 0 | 15 |
| Recommended | 18 | 3 | 21 |
| Optional | 0 | 9 | 9 |

**Overall: Core functionality complete. 3 recommended protocols blocked on wlroots upgrade.**

> Warning: **Version Constraint**: Current wlroots 0.20. Some protocols require wlroots 0.21+.

---

## Yes Essential Protocols (Complete)

These are required for basic operation. All are implemented.

| Protocol | wlroots Function | Purpose |
|----------|------------------|---------|
| `wl_compositor` | `wlr_compositor_create()` | Core surface/buffer management |
| `wl_subcompositor` | `wlr_subcompositor_create()` | Subsurface support |
| `wl_data_device_manager` | `wlr_data_device_manager_create()` | Clipboard & DnD |
| `wl_seat` | `wlr_seat_create()` | Input handling |
| `xdg_wm_base` | `wlr_xdg_shell_create()` | Application windows |
| `xdg_activation_v1` | `wlr_xdg_activation_v1_create()` | Window activation |
| `wp_fractional_scale_v1` | `wlr_fractional_scale_manager_v1_create()` | HiDPI scaling |
| `wp_viewporter` | `wlr_viewporter_create()` | Viewport transforms |
| `zwp_linux_dmabuf_v1` | `wlr_linux_dmabuf_v1_create_with_renderer()` | Zero-copy buffers |
| `zwlr_layer_shell_v1` | `wlr_layer_shell_v1_create()` | Desktop layers |
| `ext_session_lock_manager_v1` | `wlr_session_lock_manager_v1_create()` | Screen lock |
| `zwp_pointer_constraints_v1` | `wlr_pointer_constraints_v1_create()` | Pointer lock |
| `zwp_relative_pointer_v1` | `wlr_relative_pointer_manager_v1_create()` | Relative motion |
| `zwlr_output_manager_v1` | `wlr_output_manager_v1_create()` | Output configuration |
| `zxdg_output_manager_v1` | `wlr_xdg_output_manager_v1_create()` | Output logical info |

---

## Warning: Recommended Protocols (3 Missing)

### HIGH PRIORITY - Blocked on wlroots Upgrade

These protocols require **wlroots 0.21+** (currently on 0.20). Cannot implement without upgrading.

#### 1. `ext_foreign_toplevel_list_v1` 
**Status:** No UNAVAILABLE (requires wlroots 0.21+)  
**Purpose:** Window list for taskbars and panels  
**Apps Affected:** Waybar window list, any taskbar showing open windows  
**Blocked:** Header `<wlr/types/wlr_foreign_toplevel_list_v1.h>` not in wlroots 0.20

#### 2. `ext_image_copy_capture_manager_v1`
**Status:** No UNAVAILABLE (requires wlroots 0.21+)  
**Purpose:** Modern screen capture protocol  
**Apps Affected:** Newer screen recorders, OBS (modern), pipewire-screen-capture  
**Blocked:** Header `<wlr/types/wlr_image_copy_capture_manager_v1.h>` not in wlroots 0.20

#### 3. `zwp_text_input_manager_v3`
**Status:** No UNAVAILABLE (requires wlroots 0.21+)  
**Purpose:** IME support for international input  
**Apps Affected:** CJK input methods, emoji pickers  
**Blocked:** Header `<wlr/types/wlr_text_input_v3.h>` not in wlroots 0.20

**Upgrade Path:**
- wlroots 0.21 released Aug 2024
- wlroots 0.22 released Sep 2024
- wlroots 0.23 released ~Jan 2025
- Current wlroots 0.20 is from mid-2024
- Update `flake.nix` to use newer wlroots
- Will also bring additional protocol support

### Already Implemented

| Protocol | Purpose | Apps Affected |
|----------|---------|---------------|
| `wp_cursor_shape_manager_v1` | Cursor shapes | Custom cursor apps |
| `zwp_virtual_*_manager_v1` | Virtual input | On-screen keyboards, remote desktop |
| `ext_idle_notifier_v1` | Idle monitoring | Screen dimming, auto-lock |
| `zwp_idle_inhibit_manager_v1` | Idle prevention | Video players |
| `zwp_primary_selection_device_manager_v1` | Middle-click paste | Terminal selection |
| `zwlr_data_control_manager_v1` | Clipboard mgr | wl-clipboard |
| `zwlr_screencopy_manager_v1` | Screenshots | grim, wf-recorder |
| `zwlr_export_dmabuf_manager_v1` | DMA-BUF export | OBS Studio |
| `zwp_gamma_control_manager_v1` | Color temp | gammastep, wlsunset |
| `zwp_output_power_manager_v1` | Display power | Power management |
| `xdg_decoration_unstable_v1` | Decorations | Apps with CSD/SSD |
| `org_kde_kwin_server_decoration_manager` | KDE decorations | Qt apps |
| `wp_presentation` | Frame timing | Tear-free rendering |
| `wp_alpha_modifier_v1` | Alpha channel | Transparency effects |
| `wp_single_pixel_buffer_v1` | Single pixel buffers | Cursor themes |
| `wp_linux_drm_syncobj_manager_v1` | Explicit sync | Modern GPU sync |

---

## Note: Optional Protocols (Nice to Have)

| Protocol | Priority | Purpose | Use Case |
|----------|----------|---------|----------|
| `ext_workspace_manager_v1` | Medium | Workspace enumeration | External workspace switchers |
| `ext_transient_seat_manager_v1` | Medium | Nested compositors | Flatpak portals |
| `xdg_toplevel_icon_manager_v1` | Medium | App icons | Taskbar icons |
| `zwp_tablet_manager_v2` | Low | Drawing tablets | Wacom support |
| `zwp_keyboard_shortcuts_inhibit_manager_v1` | Low | Shortcut inhibition | Fullscreen games |
| `zxdg_exporter_v2` / `zxdg_importer_v2` | Low | Cross-surface refs | Portals |
| `wp_drm_lease_device_v1` | Low | DRM leasing | VR headsets |
| `wp_color_manager_v1` | Low | Color management | HDR, pro color |
| `wp_tearing_control_manager_v1` | Low | Tearing allowed | Competitive gaming |

---

## Implementation Guide

### Where to Add Protocols

All protocols are initialized in `dwl.c` in the `setup()` function (around lines 3386-3608).

### Basic Pattern

```c
// 1. Include wlroots header (usually already included)
#include <wlr/types/wlr_example_protocol_v1.h>

// 2. Add to setup() function
void setup(void) {
    // ... existing setup ...
    
    // 3. Create the protocol global
    wlr_example_protocol_v1_create(dpy);
    
    // ... rest of setup ...
}
```

### Adding Missing High-Priority Protocols (REQUIRES WLROOTS UPGRADE)

The following protocols are **NOT available in wlroots 0.20** and require upgrading to wlroots 0.21+:

```c
// Add these three lines to setup() in dwl.c (requires wlroots 0.21+):

// For taskbar window lists
wlr_foreign_toplevel_list_v1_create(dpy);

// For modern screen capture
wlr_image_copy_capture_manager_v1_create(dpy, 1);

// For international input
wlr_text_input_manager_v3_create(dpy);
```

**To upgrade wlroots:**
1. Edit `flake.nix` and update `wlroots` input to 0.21 or later
2. Run `nix flake update` to update lock file
3. Rebuild with `nix develop --command make`
4. Add the protocols above

**Note:** wlroots upgrades may require API changes in kalin-wm. Test thoroughly.

---

## Testing Protocol Support

### Check Available Protocols

```bash
# List protocols advertised by compositor
wayland-info | grep "interface:"

# Or use wlroots' debug output
WLR_DEBUG=1 ./kalin-wm 2>&1 | grep "creating"
```

### Common Issues

| Symptom | Missing Protocol | Solution |
|---------|------------------|----------|
| Window not showing in taskbar | `ext_foreign_toplevel_list_v1` | Upgrade to wlroots 0.21+ |
| Screen recording fails | `ext_image_copy_capture_manager_v1` | Upgrade to wlroots 0.21+ |
| Can't input CJK characters | `zwp_text_input_manager_v3` | Upgrade to wlroots 0.21+ |
| Game cursor stuck | `zwp_pointer_constraints_v1` | Already implemented Yes |
| No middle-click paste | `zwp_primary_selection_device_manager_v1` | Already implemented Yes |
| No screenshot/grim | `zwlr_screencopy_manager_v1` | Already implemented Yes |

---

## References

- [Wayland Protocols Repository](https://gitlab.freedesktop.org/wayland/wayland-protocols)
- [wlroots Protocol Headers](https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/master/protocol)
- [Wayland Explorer](https://wayland.app/) - Interactive protocol documentation

---

*Last updated: 2026-04-11*
*Compositors referenced: wlroots 0.20, niri, sway*
