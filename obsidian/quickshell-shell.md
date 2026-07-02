# Quickshell shell

The Quickshell shell is kalin-wm's companion desktop shell, written in QML on
Quickshell 0.3.0. It lives in `~/environment/quickshell` (a plain directory, not
part of the kalin-wm git repo). It provides the bottom bar, [[overview-mode]],
window peek, notifications, OSD, and side panels.

The shell talks to kalin-wm through two channels: the [[foreign-toplevel]]
protocol (window list and control) and the [[ipc-socket]] ([[viewport]] camera
state and commands). Its `CompositorService` picks the kalin-wm backend when
`$KALIN_IPC_SOCKET` is set, otherwise a niri backend.

The shell is now fully ported to kalin-wm:
- `Overview.qml` and `WindowPeek.qml` use `CompositorService`.
- `TaskbarService.qml` now consumes `CompositorService.windows` and uses its
  unified `activate()` / `close()` actions, so pinned/running app buttons work
  on both niri and kalin-wm.
- `WorkspaceIndicator.qml` is hidden on kalin-wm because the infinite-canvas
  model has no fixed workspaces.

A **Display** tab in the right system panel lets the user view and reorder
outputs left-to-right under niri. It uses a new `DisplayService` singleton that
polls `niri msg --json outputs` and applies positions via
`niri msg output <name> position set <x> <y>`. On kalin-wm the tab is disabled
with a placeholder.
