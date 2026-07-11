# dwl fork

- kalin-wm is a fork of dwl ("dwm for Wayland").
- dwl is the suckless, [[wlroots]]-based descendant of dwm.
- The fork keeps dwl's [[compile-time-config]] style and minimal-dependency philosophy.
- The fork has shed much of dwl's heritage: XWayland, the tiling params (mfact/nmaster), and the 9-tag workspace system are removed (see [[ledger]]).
- kalin-wm is Wayland-only with one [[infinite-canvas]] per monitor instead of tags.

## Build

- The build is still effectively a monolith, but shrinking it is an active,
  named direction — not just an aspiration. See the [[ledger]]'s
  "modularization step 1/2" entries (2026-07-06: bind-engine gesture unify,
  keyboard event dispatch extracted to `modules/input/keyboard.c`) for the
  pattern: pull real per-event logic out of `dwl.c` into its own module TU,
  leave only what genuinely can't move (lifecycle/setup code that closes over
  `dwl.c`-local statics, e.g. `keybinding()`'s `keys[]` table).
- **New functionality should default to a new module, not a `dwl.c` addition.**
  This applies concretely to the [[protocols]] work: new Wayland protocol
  support goes in `code/src/modules/protocols/`, with `dwl.c` keeping only the
  one-line `wlr_*_create()` registration call in `setup()`.
- `code/src/dwl.c` is the core translation unit and `#include`s the feature modules under `code/src/modules/` ({crop, layout, viewport, ui, input}, plus `foreign_toplevel.c` and `ipc.c`) directly.
- Independent translation units are `util.c`, `crash_report.c`, `persistence.c`, and `input/commit_size.c`.

## What kalin-wm added

- The [[infinite-canvas]] with [[world-coordinates]].
- The [[viewport]] camera.
- Originally the [[column-layout]] and [[anchored-window]] model, since replaced by the [[connection-graph]] (free positioning throughout).
- [[directional-focus]].
- [[crop-mode]].
- The [[ipc-socket]] and [[foreign-toplevel]] control.

## Suckless philosophy

- The suckless philosophy is preserved: see [[compile-time-config]].
- The full upstream-vs-fork history is in the changelog content folded into the [[ledger]].
