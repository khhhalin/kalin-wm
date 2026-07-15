# Protocols

- Catalog of Wayland protocols kalin-wm implements, and the popular ones it
  doesn't yet. Backs the [[roadmap]] item to close the gap, starting with
  `xdg-toplevel-icon-v1`.
- New protocol implementations go in `code/src/modules/protocols/` (new
  directory), **not** into `dwl.c` — see [[dwl-fork]]'s modularization
  direction. `dwl.c` currently only gets one line per protocol: the
  `wlr_*_create()` call in `setup()`; the listener/logic for anything beyond
  that trivial registration belongs in the module.

## Already implemented

Core: `wl_compositor`/`wl_subcompositor`, `xdg-shell`, `wlr-layer-shell-unstable-v1`,
`linux-dmabuf-v1`, `linux-drm-syncobj-v1`, `wp_viewporter`, `single-pixel-buffer-v1`,
`fractional-scale-v1`, `alpha-modifier-v1`.

Input: virtual-keyboard-v1, virtual-pointer-v1, pointer-constraints-v1,
relative-pointer-v1, cursor-shape-v1.

Output/session: `xdg-output-v1`, `wlr-output-management-v1`,
`wlr-output-power-management-v1` (see [[nixos-session]]), `gamma-control-v1`,
`idle-notify-v1`, `idle-inhibit-v1`, `session-lock-v1`.

Clipboard/selection: `wl_data_device`, `wlr-data-control-v1`,
`ext-data-control-v1`, `primary-selection-v1`.

Decoration: `xdg-decoration-v1`, `org_kde_kwin_server_decoration` (legacy).

Shell integration: `wlr-foreign-toplevel-management-v1` (see
[[foreign-toplevel]] — this is how the [[quickshell-shell]] gets its window
list), `xdg-activation-v1`.

Capture: `wlr-screencopy-v1`, `export-dmabuf-v1` (`code/src/modules/capture.c`),
`hyprland-toplevel-export-v1` (`code/src/modules/protocols/toplevel_export.c` —
per-window capture for Quickshell's taskbar hover-preview and [[overview-mode]]'s
grid thumbnails; hand-implemented, not a wlroots wrapper — see the [[ledger]]
entry for why this specific Hyprland-proprietary protocol was required instead of
the standard ext-image-copy-capture-v1 pair. Handles both destination-buffer
types Quickshell actually submits to `copy()` — a GPU render-pass blit for
dmabuf-backed buffers, `wlr_buffer_begin_data_ptr_access()` + CPU pixel readback
for anything else — confirmed working on both the test VM and real hardware).

## Missing — confirmed by our own logs

These three are not guesses — they're warnings `kalinwm`'s own log
(`/tmp/kalinwm.log`) prints every session, because a client asked for them and
got told no:

1. **`xdg-toplevel-icon-v1`** (`wlr_xdg_toplevel_icon_v1_create`) — lets a
   client set its own taskbar/alt-tab icon instead of the compositor guessing
   from `.desktop` lookup. **Start here** — this is the one flagged for us to
   fix first. Directly useful for the [[quickshell-shell]] taskbar/overview,
   which currently has no compositor-provided icon source.
2. **text-input-v3 + input-method-v2** (`wlr_text_input_v3.h` +
   `wlr_input_method_v2.h`) — IME plumbing. Log: *"text input interface not
   implemented by compositor; IME will be disabled."* Needed for any
   composed-input method (accents/dead-keys count, not just CJK).
3. **`xdg-system-bell-v1`** (`wlr_xdg_system_bell_v1_create`) — lets a client
   ring the system bell (terminal `\a`, etc.) through the compositor instead of
   yelling into the void. Seen in the same log family during earlier viewport
   testing this cycle.

## Missing — popular, not yet confirmed by a warning, worth doing

Checked against every `wlr_*.h` wrapper wlroots 0.20 ships
(`$(pkg-config --variable=includedir wlroots-0.20)`) against what `dwl.c`
actually calls; these have a wrapper available and are widely implemented by
other compositors (sway, hyprland, niri) but kalin-wm has no `_create()` call
for them at all:

- **`tearing-control-v1`** (`wlr_tearing_control_v1.h`) — lets a fullscreen
  game/video request tearing for lower latency. Cheap to add, real latency win.
- **`content-type-v1`** (`wlr_content_type_v1.h`) — a surface hints "I'm
  video" vs "I'm UI," useful for render scheduling. mpv and browsers set this.
- **`xdg-dialog-v1`** (`wlr_xdg_dialog_v1.h`) — marks a toplevel as a modal
  dialog of its parent. Increasingly set by GTK4 apps.
- **`xdg-foreign-v1`/`v2`** (`wlr_xdg_foreign_v1.h`, `_v2.h`,
  `wlr_xdg_foreign_registry.h`) — lets one client reference another's surface
  as a parent (portals, file-chooser-style flows).
- **`keyboard-shortcuts-inhibit-v1`** (`wlr_keyboard_shortcuts_inhibit_v1.h`)
  — lets a client (remote desktop, VNC, a game) ask to receive all keys
  unintercepted, with an escape hatch bind to break out. Standard in sway,
  hyprland, niri.
- **`security-context-v1`** (`wlr_security_context_v1.h`) — sandboxing label
  for proxied/portal-spawned clients (flatpak-adjacent). Wanted by
  `xdg-desktop-portal` integration generally.

## Explicitly not planned

- **`pointer-gestures-unstable-v1`** (touchpad swipe/pinch) — the [[roadmap]]
  already dropped smooth-animation/gesture work on 2026-06-26; skip.
- **`ext-workspace-v1`** — a workspace-listing protocol; doesn't fit kalin-wm's
  tag-less [[infinite-canvas]] model (no fixed workspaces to list). Skip unless
  that model changes.
- **`ext-foreign-toplevel-list-v1`**, **`ext-image-copy-capture-v1`** — newer
  standardized replacements for protocols we already have working
  (`wlr-foreign-toplevel-management-v1`, `wlr-screencopy-v1`) and the
  [[quickshell-shell]] already speaks. Modernization, not a gap; low priority.
