# Protocol Matrix

> Complete implementation status of Wayland protocols in kalin-wm.

## Summary

| Category | Implemented | Missing | Total |
|----------|-------------|---------|-------|
| Essential | 15/15 | 0 | 15 |
| Recommended | 18/21 | 3 | 21 |
| Optional | 0/9 | 9 | 9 |

**Version Constraint**: Current wlroots 0.20. Three recommended protocols require wlroots 0.21+.

---

## Essential Protocols (15/15)

Required for basic operation. All implemented.

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

## Recommended Protocols (18/21)

### Blocked on wlroots Upgrade

These require **wlroots 0.21+** (currently on 0.20):

| Protocol | Purpose | Affected Apps |
|----------|---------|---------------|
| `ext_foreign_toplevel_list_v1` | Taskbar window lists | Waybar, panels |
| `ext_image_copy_capture_manager_v1` | Modern screen capture | OBS, screen recorders |
| `zwp_text_input_manager_v3` | IME/CJK input | fcitx, ibus |

### Implemented

| Protocol | Purpose | Affected Apps |
|----------|---------|---------------|
| `wp_cursor_shape_manager_v1` | Cursor shapes | Custom cursor apps |
| `zwp_virtual_*_manager_v1` | Virtual input | On-screen keyboards |
| `ext_idle_notifier_v1` | Idle monitoring | Screen dimming, auto-lock |
| `zwp_idle_inhibit_manager_v1` | Idle prevention | Video players |
| `zwp_primary_selection_device_manager_v1` | Middle-click paste | Terminals |
| `zwlr_data_control_manager_v1` | Clipboard mgr | wl-clipboard |
| `zwlr_screencopy_manager_v1` | Screenshots | grim, wf-recorder |
| `zwlr_export_dmabuf_manager_v1` | DMA-BUF export | OBS Studio |
| `zwp_gamma_control_manager_v1` | Color temp | gammastep, wlsunset |
| `zwp_output_power_manager_v1` | Display power | Power management |
| `xdg_decoration_unstable_v1` | Decorations | CSD/SSD apps |
| `org_kde_kwin_server_decoration_manager` | KDE decorations | Qt apps |
| `wp_presentation` | Frame timing | Tear-free rendering |
| `wp_alpha_modifier_v1` | Alpha channel | Transparency |
| `wp_single_pixel_buffer_v1` | Single pixel buffers | Cursors |
| `wp_linux_drm_syncobj_manager_v1` | Explicit sync | GPU sync |

---

## Optional Protocols (0/9)

Nice to have, not critical for core functionality.

| Protocol | Priority | Purpose | Use Case |
|----------|----------|---------|----------|
| `ext_workspace_manager_v1` | Medium | Workspace enumeration | External switchers |
| `ext_transient_seat_manager_v1` | Medium | Nested compositors | Flatpak portals |
| `xdg_toplevel_icon_manager_v1` | Medium | App icons | Taskbar icons |
| `zwp_tablet_manager_v2` | Low | Drawing tablets | Wacom support |
| `zwp_keyboard_shortcuts_inhibit_manager_v1` | Low | Shortcut inhibition | Fullscreen games |
| `zxdg_exporter_v2` / `importer_v2` | Low | Cross-surface refs | Portals |
| `wp_drm_lease_device_v1` | Low | DRM leasing | VR headsets |
| `wp_color_manager_v1` | Low | Color management | HDR, pro color |
| `wp_tearing_control_manager_v1` | Low | Tearing allowed | Competitive gaming |

---

## Testing Protocol Support

```bash
# List protocols advertised by compositor
wayland-info | grep "interface:"

# Or use wlroots debug output
WLR_DEBUG=1 ./kalin-wm 2>&1 | grep "creating"
```

---

## Upgrade Path

To unblock the 3 missing recommended protocols:

1. Update `flake.nix` to use wlroots 0.21+
2. Run `nix flake update`
3. Rebuild with `nix develop --command make`
4. Add protocol initializers to `setup()`

**Note**: wlroots upgrades may require API changes in kalin-wm.

---

*Last updated: 2026-04-09*
