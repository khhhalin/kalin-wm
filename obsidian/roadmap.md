# Roadmap

Planned and open work for [[kalin-wm]]. Completed work and decisions go in the
[[ledger]]; this note is the forward-looking backlog. It supersedes the old root
`ROADMAP.md`.

## Direction (2026-07-01)

The goal is to **daily-drive** kalin-wm on the [[infinite-canvas]], with
**horizontal column scrolling as the primary motion** ([[column-layout]]) and 2D
freedom as a secondary/optional convenience. Current priorities, in order:

1. **[[stability]] / crash-proofing** — the main blocker to daily use.
2. **[[quickshell-shell]] integration** — taskbar, [[overview-mode]], notifications.
3. **Keep cutting dwl heritage** — continue simplifying inherited code
   (XWayland, tiling params, and tags already removed — see [[ledger]]).

[[zoom]] is **parked** — dropped from active focus and being rethought.
Target: laptop screen first, occasional external monitor second.

## Blocking v1.0

- [[stability]] — Phase 0 audit (all 23 items fixed and re-verified). ✅ done.
  Ongoing crash-proofing continues as the top daily-drive priority.

## In progress

- Verify the [[nixos-session]] end-to-end after `nixos-rebuild switch`:
  quickshell bar auto-starts, `Super+T`/`Super+P`/`Super+O` work, and the taskbar
  lists running apps.

## Recently completed (see [[ledger]])

- Ported the [[quickshell-shell]] to the kalin-wm backend: `TaskbarService` and
  window actions now use `CompositorService`/[[foreign-toplevel]];
  `WorkspaceIndicator` is hidden on kalin-wm.
- Wired the [[nixos-session]] to start the shell + terminal via the
  `kalin-wm-session` wrapper.

## v1.0 features — open

- [[window-menu]] — keybind-triggered [[quickshell-shell]] per-window action menu
  (resize, opacity, anchor/pin, close, fullscreen), sitting **alongside** the
  existing window keybinds (not replacing them). Compositor mechanics for all
  five actions now exist as keybinds (**opacity** wired to
  `wlr_scene_buffer_set_opacity`, `Super+D`); still to do: expose them as
  [[ipc-socket]] commands and build the Quickshell menu surface.
- Window shadows.
- [[overview-mode]] as a compositor decision (deferred — see research note).

## Post-v1.0 — nice to have

- Rounded corners.
- Minimap (corner overview of all windows + viewport rectangle).
- Bookmarks (named [[viewport]] positions to jump to).
- Magnetic snapping (windows snap to each other / to a grid).
- Anchor-mode visual distinction (different border for an [[anchored-window]]).
- [[crop-mode]] on-screen banner; cursor-state feedback during pan/move/resize.

## Dropped

- Smooth animations (spring physics) and touchpad gestures — removed from the
  backlog on 2026-06-26 (see [[ledger]]).

## Already shipped (see [[ledger]])

[[infinite-canvas]], [[viewport]] [[pan]]/[[zoom]]/[[follow-mode]]/[[fit-all]],
[[buffer-scaling]], [[column-layout]] + [[anchored-window]], [[directional-focus]],
[[crop-mode]], [[focus-ring]], [[off-screen-indicators]], [[persistence]],
[[ipc-socket]], [[foreign-toplevel]], multi-monitor.
