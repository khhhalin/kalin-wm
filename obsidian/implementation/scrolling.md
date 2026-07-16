# Scrolling

- How wheel/touchpad scroll events reach clients, and the knobs that shape
  them. Camera panning is separate — see [[pan]] and [[gestures]].
- All knobs are [[compile-time-config]] in `code/config/config.h`
  (defaults in `config.def.h`).

## Event path

- `axisnotify()` (`code/src/dwl.c`) receives every `wlr_pointer_axis_event`.
  Modifier+wheel scroll binds get first refusal (`bind_dispatch_scroll`,
  see [[keybindings]]); everything else is scaled/smoothed and forwarded
  to the focused client via `wlr_seat_pointer_notify_axis()`.

## Knobs

- `natural_scrolling` (0/1) — libinput-level direction flip, applied per
  device in `createpointer()`. On since 2026-07-16 (user preference).
- `scroll_factor_finger` / `scroll_factor_wheel` — multiply `delta` (and
  `delta_discrete`, rounded) before clients see them; < 1.0 slower,
  > 1.0 faster. Finger covers `FINGER` + `CONTINUOUS` sources; wheel
  covers `WHEEL` + `WHEEL_TILT`. Sway-style caveat applies: a non-1.0
  wheel factor makes `delta_discrete` a non-multiple of 120, which some
  clients round oddly — prefer tuning the finger factor.
- `scroll_smoothing` (0.0–<0.9) — exponential moving average over
  successive **finger** deltas only (per orientation, wheels never
  smoothed): `out = s·prev + (1−s)·in`. Filters touchpad jitter at the
  cost of a slightly floatier feel. A zero finger delta is the
  scroll-stop event: forwarded untouched (clients key kinetic scrolling
  off it) and it resets the filter so the next flick starts clean.
  0.3 in config.h (anti-jitter, 2026-07-16); 0.0 (off) in config.def.h.
