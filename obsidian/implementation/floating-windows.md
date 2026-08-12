# Floating windows

- kalin-wm free-positions every window on the [[infinite-canvas]] — there is no
  tiled/floating split for ordinary windows. "Floating" here means one specific
  thing: **dialog/transient/modal/fixed-size popups render as screen-space
  overlays** that stay a fixed 1:1 size on top, instead of panning/zooming with
  the canvas like normal windows.

## What floats

- Detection is `client_is_float_type()` (`client_inline.h`): an xdg-shell
  **parent** (a dialog/transient-for its opener) OR a **fixed size**
  (`min_width == max_width` or `min_height == max_height`, i.e. a modal/fixed
  dialog). This helper existed but was dead until the floating feature used it.

## What floating does (mapnotify, `dwl.c`)

- Sets `c->isfloating` and, at map:
  - **Centers** the window on its parent's on-screen rect (if it has a parent)
    or the monitor work area (screen space).
  - **Camera-exempt:** `c->isfloating` is added to the screen-space bypass in
    `client_apply_zoom_frame()` and `client_set_buffer_scale()` (the same bypass
    `docked`/`isfullscreen`/`ismaximized` use), so pan/zoom don't move or scale
    it — it stays a 1:1 overlay. This is what makes it *float* rather than be
    "just another canvas window".
  - **Auto-focuses** it (`focusclient(c, 1)`) — a modal prompt (sudo/zenity, a
    file chooser) must be actionable immediately, unlike ordinary spawned
    windows which deliberately don't steal focus.
  - **Skips** the spawn cascade, the [[connection-graph]] link, and
    camera-follow (`viewport_center_on`) — a screen-space overlay is already
    where the user is looking.

## Status

- Shipped 2026-08-12 (branch `floating-dialogs`). Motivated by a sudo/zenity
  askpass prompt that behaved like a canvas window (panned away, uncentered).
- **Verified in the [[test-vm]]** (see AGENTS.md's vmctl recipe): a Firefox
  `Ctrl+O` file-chooser (a transient child) floated centered and focused, and
  stayed pixel-identical through 6 canvas zoom-outs while Firefox itself shrank
  — an independent visual assessor confirmed the screen-space behavior.
- Layer: stays in `LyrFloat`, raised to the top of its layer by the auto-focus.
  Not moved to `LyrFloatTop` — so it's above the currently-focused canvas window
  while open, but a later focus elsewhere can raise a canvas window over it
  (standard click-to-raise). Promote to `LyrFloatTop` if "always above tiling"
  is wanted later.
