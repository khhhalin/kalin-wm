# Screenshot UI

- Niri-style interactive screenshot tool, bound to `Super+Shift+S` (see [[keybindings]]); also openable via the `screenshot-ui` [[ipc-socket]] command (added 2026-07-15, alongside `screenshot` for the immediate Super+Print-style capture).
- **Freeze-frame (2026-07-15)**: opening renders the monitor's scene once (`capture_render_native`) and displays it as a scene buffer under the dim — the world visibly stops while selecting, and confirm crops from those frozen pixels (`capture_export_pixels`), so the capture is exactly what was frozen. This replaced the old confirm-time re-render, which ran *before* the overlay was destroyed and baked the 35% dim into every saved screenshot.
- **TUI-styled readout (2026-07-15)**: a bottom-center panel shows the live selection `W X H  AT (X,Y)` and the key hints, in the [[bar-tuis]] warm-amber palette. Rasterized with a hand-rolled 5x7 bitmap font into an owned-pixels `wlr_buffer` (`pixel_buffer_*` in screenshot_ui.c — the buffer frees its pixels only when the renderer drops its last lock). Re-rendered only when the text changes during a drag.
- Key scheme matches niri's screenshot UI exactly (reverse-engineered from niri's `src/ui/screenshot_ui.rs`, not literally ported — different languages/toolkits, same UX): `Escape` cancels; `Space`/`Enter` confirms (disk + clipboard); `Ctrl+C` confirms clipboard-only; `P` toggles pointer visibility (currently a state flag only — captures never include the pointer either way, since the underlying render path doesn't draw the cursor).
- Implemented in `code/src/modules/screenshot/screenshot_ui.c` (UI/selection state, freeze, info panel) + `capture.c`'s `capture_render_native()`/`capture_export_pixels()` (render, crop, PNG encode, disk write, clipboard). Keypress interception for the four UI keys lives in `code/src/modules/input/keyboard.c`, following the same "bare key while a mode is active" pattern as crop-mode's `r` and overview's `Escape`.
- Selection coordinates live in the same screen-pixel space as `CropEditor`'s (`cursor->x/y`, matching `Monitor.m`); the export maps that to the native render buffer's physical pixels via `cw / m.width` / `ch / m.height` scale factors, since the render buffer is native (possibly HiDPI-scaled) resolution while cursor coordinates are logical/layout pixels.
- Disk saves go to `~/Pictures/Screenshots/Screenshot from <timestamp>.png`, matching niri's convention.

## The clipboard deadlock (fixed 2026-07-10)

The first implementation piped the PNG bytes directly into `wl-copy`'s stdin from the compositor's own process, blocking on `write()` until the child drained the pipe. That deadlocks: `wl-copy` needs to round-trip over Wayland with *this same compositor* to register the clipboard data-control source, but the single-threaded event loop can't service that handshake while blocked inside `write()` — and a multi-MB PNG vastly exceeds the ~64KB pipe buffer, so it never drains. Reproduced live in the [[test-vm]]: after confirming a capture, the compositor's framebuffer and pointer both stopped updating entirely (verified via QMP screenshot hashes staying identical across pointer-move commands).

Fix: never let the compositor write clipboard bytes itself. `capture_export_selection()` always lands the PNG on disk first (the real save path if `to_disk`, otherwise a temp file under `$XDG_RUNTIME_DIR`), then `capture_copy_to_clipboard()` does a non-blocking `fork()+exec("sh", "-c", "wl-copy --type image/png < \"$1\"; rm -f \"$1\"")` — no bytes ever pass through the compositor process itself, so the event loop is never blocked.

## Hover crash + fade-on-approach (found 2026-07-18, fixed 2026-07-19)

- **Bug (fixed): hovering the info panel crashed the compositor.** `xytonode()`
  (`code/src/dwl.c`) treated every `WLR_SCENE_NODE_BUFFER` under the cursor as
  a Wayland surface — `wlr_scene_surface_try_from_buffer(...)->surface` — but
  the info readout is a **plain pixel buffer**, so `try_from_buffer()` returned
  NULL and `->surface` segfaulted. `motionnotify()` runs `xytonode()` on every
  pointer motion while the UI is active, so the cursor touching the
  bottom-center panel crashed instantly. The dim/border are `wlr_scene_rect`s
  (type RECT) and were never affected; only the info buffer (and the
  whole-monitor frozen-frame buffer, when it was the topmost hit) triggered it.
- **Fix, two complementary parts (both shipped):**
  1. **The actual fix** — `xytonode()` now NULL-checks the result of
     `wlr_scene_surface_try_from_buffer()` before dereferencing `->surface`.
     Guards a whole *class* of bug (any non-surface overlay buffer), per the
     [[stability]]/defensive-C rule.
  2. **Belt-and-suspenders** — the info and frozen-frame `wlr_scene_buffer`s
     now get a `point_accepts_input` callback
     (`screenshotui_node_rejects_input()` in `screenshot_ui.c`) that returns
     false, exactly like paper mode's `paper_node_rejects_input()` (`dwl.c`),
     so `wlr_scene_node_at()` skips these decorative buffers instead of
     hitting them — they also no longer steal focus/clicks from whatever's
     drawn behind them.
- **Shipped: fade the info panel as the cursor approaches.**
  `screenshotui_hover()` (`screenshot_ui.c`) drives
  `wlr_scene_buffer_set_opacity()` on the info node by nearest-point distance
  from the cursor to the panel's cached bounding box
  (`ScreenshotEditor.info_{x,y,w,h}`, `code/include/kalin.h`): opacity ramps
  1.0 → `INFO_FADE_FLOOR` (0.15) as distance closes from `INFO_FADE_RADIUS`
  (220px) to 0, keeping the readout legible rather than hiding it. Called from
  three places so it stays current: after (re)creating the info node in
  `screenshotui_info_update()`, on the early-return path there when text is
  unchanged but the cursor may have moved, and from `dwl.c`'s `motionnotify()`
  on every motion tick while the UI is active but not dragging (the
  drag-time path already runs through `screenshotui_info_update()` via
  `screenshotui_draw()`). Opacity is unrelated to hit-testing — it does not
  substitute for fix part 1 above.
