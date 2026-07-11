# Trackpad gestures

- Trackpad gesture navigation for the [[viewport]] camera: a 3-finger swipe
  pans, a pinch zooms. Added 2026-07-09, inspired by [[driftwm]]'s
  gesture-driven canvas navigation (kalin-wm had zero touchpad gesture
  support before this).
- New module `code/src/modules/input/gestures.c`, `gestures_attach()`
  called from `createpointer()` (`code/src/dwl.c`) for every pointer
  device — a plain mouse just never emits the events, so it's a harmless
  no-op set of listeners for one.
- Uses wlroots' native `wlr_pointer.events.swipe_begin/update/end` and
  `pinch_begin/update/end` signals directly — the same libinput gesture
  recognition a touchpad's pointer device already does. No new Wayland
  protocol dependency, nothing exposed to clients.
- **Swipe (3 fingers) pans**: content follows the fingers, same convention
  as the existing `Super+Ctrl+LMB` direct-manipulation pan-grab
  (`viewport_pan_grab_update()`). Live during the gesture — bypasses the
  normal target-lerp easing entirely (`viewport.animating`/`coasting` are
  both cleared on `swipe_begin` so the fingers and the camera never fight).
- **Momentum ("flick") panning**: `viewport_coast_start()`
  (`code/src/modules/viewport/viewport_ops.c`), called from `swipe_end` with
  a velocity estimate (exponential moving average of the swipe's recent
  per-update deltas). No-ops below `COAST_MIN_START_SPEED` — a slow,
  deliberate-stop swipe doesn't drift on. While coasting, `viewport_tick()`
  integrates `viewport.vel_x/y` each fixed step and decays it by
  `COAST_FRICTION_PER_SEC`, stopping once speed drops below
  `COAST_STOP_SPEED` — a separate physics model from the existing
  target-lerp easing, both driven by the same `viewport.animating` flag so
  `rendermon()`'s "keep scheduling frames while animating" loop needs no
  changes.
- **Pinch zooms**: `viewport.zoom` set directly from the pinch's cumulative
  `scale` factor (relative to `zoom` at `pinch_begin`), clamped to the same
  0.1–5.0 range `viewport_zoom()` uses. No momentum on zoom.
- **Testing limitation**: the [[test-vm]] (QEMU) has no synthetic multi-
  finger/gesture input — QMP's `input-send-event` only supports key/button/
  axis events, not libinput gesture frames — so swipe/pinch recognition
  itself can only be verified on real trackpad hardware. VM testing for
  this change was limited to confirming the build is clean, unit tests
  pass, and normal mouse/keyboard interaction still works with the new
  listeners attached (no regression in the common no-touchpad case).
