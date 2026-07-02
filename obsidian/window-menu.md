# Window menu (per-window action menu)

**Status: planned — not yet implemented.**

The window menu is a per-window action panel, opened on the focused window with a
keybind. It gives that window an "android-like" control surface: instead of
memorising a separate keybind for every window operation, you open one menu and
pick the action. It is a discoverable, mouse/touch-friendly alternative that
**sits alongside** the existing window [[keybindings]] — the current sizing/action
binds stay; the menu is an addition, not a replacement.

It is implemented as a **[[quickshell-shell]] surface**, driven over the
[[ipc-socket]] — the compositor exposes the focused window's state and the actions,
and Quickshell draws and handles the menu.

## What it exposes

- **Resize** — adjust the window's size (and its column width/height on the
  [[column-layout]]). Mirrors the existing `Super+[` / `Super+]` /
  `Super+Shift+{}` sizing binds, which stay in place.
- **Opacity** — per-window transparency, dialled up/down. **Compositor mechanism
  now implemented** (2026-07-01): each [[client|Client]] has an `opacity` field
  (0.1–1.0) applied to its scene buffers via `wlr_scene_buffer_set_opacity`,
  re-applied on commit; bound to `Super+D` / `Super+Shift+D`. Still needed for the
  menu: an [[ipc-socket]] command to set it from Quickshell.
- **Anchor / pin** — toggle the window as an [[anchored-window]] so it stays fixed
  in the [[viewport]] while the canvas scrolls.
- **Close** and **fullscreen** — basic lifecycle actions.

## Interaction

- **Trigger:** a keybind opens the menu for the focused window (gesture support is
  a possible later addition, not required).
- Actions apply to the focused window and update live (e.g. opacity preview while
  adjusting).

## Why

kalin-wm is aimed at being a [[kalin-wm|daily driver]] on the [[infinite-canvas]]
with horizontal column scrolling as the primary motion. A single discoverable
per-window menu fits that better than a growing set of one-off chords, and keeps
window control fast on a laptop.

## Open questions

- The [[ipc-socket]] additions needed: expose the focused window's opacity/size/
  anchor state, and accept set-opacity / resize / anchor / close / fullscreen
  commands (the socket is currently camera-only).
- Whether resize in the menu is stepwise or a continuous drag.
- Default keybind.

Related: [[anchored-window]], [[crop-mode]], [[column-layout]], [[keybindings]],
[[roadmap]].
