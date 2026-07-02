# dwl fork

kalin-wm is a fork of dwl ("dwm for Wayland"). dwl is itself the suckless,
[[wlroots]]-based descendant of dwm. The fork keeps dwl's [[compile-time-config]]
style and minimal-dependency philosophy, but has shed much of dwl's heritage:
XWayland, the tiling params (mfact/nmaster), and the 9-tag workspace system are
removed (see [[ledger]]). kalin-wm is Wayland-only with one [[infinite-canvas]]
per monitor instead of tags.

The build is still effectively a monolith: `code/src/dwl.c` is the core
translation unit and `#include`s the feature modules under `code/src/modules/`
({crop, layout, viewport, ui, input}, plus `foreign_toplevel.c` and `ipc.c`)
directly. Independent translation units are `util.c`, `crash_report.c`,
`persistence.c`, and `input/commit_size.c`.

What kalin-wm added on top of dwl: the [[infinite-canvas]] with
[[world-coordinates]], the [[viewport]] camera, the [[column-layout]] and
[[anchored-window]] model, [[directional-focus]], [[crop-mode]], the
[[ipc-socket]], and [[foreign-toplevel]] control.

The suckless philosophy is preserved: see [[compile-time-config]]. The full
upstream-vs-fork history is in the changelog content folded into the [[ledger]].
