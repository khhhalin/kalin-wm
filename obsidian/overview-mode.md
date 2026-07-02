# Overview mode

Overview mode is an exposé grid of all windows. It is triggered by `Super+O`,
which spawns `qs ipc call windows-bar toggleOverview` — i.e. kalin-wm asks the
[[quickshell-shell]] to show the overview rather than rendering it itself.

The overview is a shell feature: it lists windows from `CompositorService` and
draws thumbnails with `ScreencopyView` over the [[foreign-toplevel]] handles.

On kalin-wm the shell now uses the kalin backend via `CompositorService`, so the
overview lists windows and can draw thumbnails from the [[foreign-toplevel]]
handles. Validation checklist: after login, `Super+O` toggles the overview grid.

As a compositor feature, native overview rendering is a deferred decision; see
[[research/active-design/overview-mode|the overview-mode note]].
