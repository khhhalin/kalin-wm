# screenshot-crash-fade — report

## Summary

Fixed the compositor crash triggered by hovering the screenshot UI's
bottom-center info panel, added the belt-and-suspenders input-reject
callback, and implemented fade-on-approach for the info panel.

## Changes

**`code/src/dwl.c`**
- `xytonode()`: NULL-check the return of `wlr_scene_surface_try_from_buffer()`
  before dereferencing `->surface`. This is the actual crash fix — the info
  readout is a plain pixel buffer (not a Wayland surface), so
  `try_from_buffer()` legitimately returns NULL for it, and the old code
  dereferenced NULL unconditionally.
- `motionnotify()`: added a call to the new `screenshotui_hover()` (falls
  through, doesn't return) whenever `screenshot_ui.active`, so the fade
  updates on every pointer motion tick while the UI is open, not just during
  a drag (the existing `screenshotui_draw()` call only fires mid-drag).
- Added `screenshotui_hover(void)` to both the internal prototype block and
  `code/include/kalin.h`'s public prototype list.

**`code/src/modules/screenshot/screenshot_ui.c`**
- Added `screenshotui_node_rejects_input()` (mirrors `dwl.c`'s
  `paper_node_rejects_input()`), wired as `point_accepts_input` on both the
  info node and the frozen-frame node — the tidy/belt-and-suspenders part of
  the fix, independent of the `xytonode()` NULL check.
- Added `INFO_FADE_RADIUS` (220px) and `INFO_FADE_FLOOR` (0.15) constants.
- `screenshotui_info_update()` now caches the info panel's bounding box into
  new `ScreenshotEditor` fields (`info_x/y/w/h`) and calls
  `screenshotui_hover()` after creating a new node, and also on the
  early-return path when the text is unchanged (geometry didn't change, but
  the cursor may have moved closer/farther).
- New `screenshotui_hover()`: computes nearest-point distance from the cursor
  to the cached info-panel box, maps it linearly to an opacity in
  `[INFO_FADE_FLOOR, 1.0]` over `INFO_FADE_RADIUS`, and applies it via
  `wlr_scene_buffer_set_opacity()`.

**`code/include/kalin.h`**
- `ScreenshotEditor` gained `info_x, info_y, info_w, info_h` (cached panel
  geometry for the hover distance check).
- Added `screenshotui_hover(void)` to the public prototype list.

**`obsidian/implementation/screenshot-ui.md`**
- Moved the "Hover crash + fade-on-approach" section from "found, fix not yet
  built" to "found/fixed", describing the shipped fix and fade mechanism
  (function/field names, constants, call sites) in place of the prior plan
  language.

## Reasoning on the crash fix (not live-verified — see note below)

Before: `xytonode()` did
```c
surface = wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node))->surface;
```
unconditionally for any `WLR_SCENE_NODE_BUFFER` scene node under the cursor.
`wlr_scene_surface_try_from_buffer()` returns NULL for any scene buffer that
isn't backed by a `wlr_surface` — which is exactly the case for the info
panel (rasterized into an owned-pixels `wlr_buffer` via `pixel_buffer_create`,
see `screenshotui_info_update()`) and the frozen-frame buffer. Dereferencing
`->surface` on that NULL return is an unconditional NULL-pointer dereference,
i.e. a guaranteed segfault the instant `xytonode()` is called with the cursor
over either buffer. `motionnotify()` calls `xytonode()` unconditionally on
every pointer-motion event, and the info panel is always present and hit-
testable at bottom-center whenever the screenshot UI is open, so the crash
was deterministic on hover, matching the reported bug.

After the fix, `wlr_scene_surface_try_from_buffer()`'s result is stored and
checked before use; `surface` is left NULL (its initialized value) when the
node isn't a real Wayland surface, exactly the same as the pre-existing
"nothing under the cursor" case the rest of `xytonode()`/callers already
handle (e.g. the `!surface` check further down in `motionnotify()`). No
other code path changes.

## Verification

- `nix develop -c make clean all`: exits 0, no new warnings (one pre-existing
  `-Woverlength-strings` warning in `default_binds.h`, unrelated).
- `nix develop -c make test-unit`: all suites pass (Viewport-Ops, Bind DSL 25/25,
  Window-Shader Math), 0 assertion failures.
- The crash itself was **not** reproduced live — per the task brief, hover
  reproduction requires a live/VM session, which is keeper-only verification.
  The fix is a mechanical NULL-check of a call whose NULL-return path was
  previously unconditionally dereferenced; I'm confident in the logic but
  have not observed the crash disappear firsthand.
- Did not manually verify the fade visually (also requires a live session).
  The opacity math was sanity-checked by hand: at distance 0 (cursor inside
  the box) alpha = `INFO_FADE_FLOOR` = 0.15; at distance ≥ 220px alpha = 1.0;
  linear in between.

## Scope

Touched only `code/src/dwl.c`, `code/src/modules/screenshot/screenshot_ui.c`,
`code/include/kalin.h` (not originally listed in scope, but required — the
shared `ScreenshotEditor` struct and screenshotui_* prototypes live there,
same pattern the existing struct/prototype fields already follow), and the
vault note. No other files changed.
