# Roadmap

- Planned and open work for [[kalin-wm]].
- Completed work and decisions go in the [[ledger]]; this note is the forward-looking backlog.
- It supersedes the old root `ROADMAP.md`.

## Direction (2026-07-01)

- The goal is to **daily-drive** kalin-wm on the [[infinite-canvas]]. The
  original plan here named the (since-removed) [[column-layout]] as the
  primary motion; the actual current model is full free positioning + a
  [[connection-graph]] between spawn-adjacent windows (2D from the start,
  not column-scrolling-first) — see [[connection-graph]] and the [[ledger]]
  for when/why that changed.
- Current priorities, in order:
1. **[[stability]] / crash-proofing** — the main blocker to daily use.
2. **[[quickshell-shell]] integration** — taskbar, [[overview-mode]], notifications.
3. **Keep cutting dwl heritage** — continue simplifying inherited code (XWayland, tiling params, and tags already removed — see [[ledger]]).
   - Modularization (splitting `dwl.c` into `code/src/modules/*` TUs) is
     ongoing, not finished: 5169 -> 4171 lines in one 2026-07-09 sweep
     (connection-graph, directional-focus, client-anim, PTY, window-size-
     history all extracted — see [[ledger]] and [[connection-graph]]), but
     dwl.c had also grown back up from a prior 3884-line low as fast as
     features landed. Next candidates once more accumulates: `main()`/
     `setup()`/the wlroots-object-lifecycle listener handlers are the
     intentional irreducible core and shouldn't be targeted.

- [[zoom]] is **parked** — dropped from active focus and being rethought.
- Target: laptop screen first, occasional external monitor second.

## Blocking v1.0

- [[stability]] — Phase 0 audit (all 23 items fixed and re-verified). ✅ done.
- Ongoing crash-proofing continues as the top daily-drive priority.

## In progress

- **[[multi-camera]] — independent per-monitor viewports** (started 2026-07-15):
  single-view shared world, camera ops follow the cursor's monitor, drag
  hand-off + send-to-monitor bind. Design + touch list in the note.

- Verify the [[nixos-session]] end-to-end after `nixos-rebuild switch`: quickshell bar auto-starts, `Super+T`/`Super+P`/`Super+O` work, and the taskbar lists running apps.
- **[[protocols]] — implement missing popular Wayland protocols**, starting
  with `xdg-toplevel-icon-v1` (confirmed missing: our own log warns
  `compositor does not implement the xdg-toplevel-icon protocol` every
  session). See [[protocols]] for the full missing/implemented breakdown and
  priority order.
  - **Rule for this work (and any future protocol addition): write it as a
    new module under `code/src/modules/protocols/`, not into `dwl.c`.**
    `dwl.c` keeps only the one-line `wlr_*_create()` registration call in
    `setup()`; the listener setup and logic go in the module. This continues
    the modularization direction already tracked in the [[ledger]]
    ("modularization step 1/2") and in [[dwl-fork]] — the goal is to shrink
    the monolith, not grow it every time we add a protocol.

## Recently completed (see [[ledger]])

- [[bar-tuis]]: custom Textual TUIs for all seven docked bar panels (wifi/bluetooth/mixer/stats/clipboard/battery/display), battery out of the QML SidePanel, DockedPanel lifecycle races fixed, and the ipc.c broadcast line-framing fix. Awaiting the home-config rebuild that puts `kalin-bar-tui` on PATH.
- Ported the [[quickshell-shell]] to the kalin-wm backend: `TaskbarService` and window actions now use `CompositorService`/[[foreign-toplevel]]; `WorkspaceIndicator` is hidden on kalin-wm.
- Wired the [[nixos-session]] to start the shell + terminal via the `kalin-wm-session` wrapper.
- Replaced [[column-layout]]/[[anchored-window]] entirely with free
  positioning + the [[connection-graph]] (spawn-adjacency, group-drag,
  directional swap, splice-on-close, growth-overlap push).
- [[window-menu]] implemented (`hold Super`, `WindowActions.qml`).
- [[connection-graph]]: forgiving drag-to-cut severing (was a precise
  single-click) and a menu-armed way to manually create a connection
  (`Super+L`, was automatic-only) — see the "Manual create/sever" section.
- Trackpad gesture navigation: 3-finger swipe pans (with momentum coast),
  pinch zooms — see [[gestures]].
- [[persistence]] reworked: multi-instance identity keying (was colliding
  same-appid windows onto one saved slot), connection-graph save/restore,
  and a `mkdir -p` fix for a bug that had silently disabled persistence
  entirely since it was introduced.
- Resize (`Super+BTN_RIGHT`) now grabs whichever corner of the window is
  nearest the cursor instead of always the bottom-right one; added
  `Super+Ctrl+BTN_LEFT` solo move (move a window without dragging its
  connected component along, without severing) — see [[connection-graph]].

## v1.0 features — open

- Window shadows.

## Post-v1.0 — nice to have

- Rounded corners.
- Minimap (corner overview of all windows + viewport rectangle).
- Bookmarks (named [[viewport]] positions to jump to).
- Magnetic snapping (windows snap to each other / to a grid).
- Anchor-mode visual distinction (different border for an [[anchored-window]]).
- [[crop-mode]] on-screen banner; cursor-state feedback during pan/move/resize.

## Under consideration — auto-pan when dragging past the viewport edge

- The other half of the driftwm-inspired item below: **3-finger swipe pan +
  momentum coast + pinch zoom is now shipped** (2026-07-09, see [[gestures]]
  and the [[ledger]]) and out of this section.
- Still open: auto-pan when dragging a window past the viewport edge (also a
  driftwm feature) — would extend `motionnotify()`'s `CurMove` branch.
  Investigated, not yet implemented.
- Note: "smooth animations (spring physics)" from the 2026-06-26 drop below
  is actually partially shipped, not fully dropped — group-drag and
  `swap_neighbor_dir()` already glide clients via a spring animation (see
  [[connection-graph]]). Only camera-level spring/momentum motion was open,
  and gesture-driven momentum panning now covers that case; edge-drag
  auto-pan is the one piece still outstanding.

## Dropped

- Touchpad gestures were removed from the backlog on 2026-06-26 — see the
  "Under consideration" section above for why that's being revisited.

## Known minor bugs (found 2026-07-09, not yet fixed)

- Cursor icon isn't reset to the pointer focus's own preferred shape after a
  move/resize/pan interaction ends — forced back to "default" instead
  (`code/src/dwl.c`, upstream dwl-heritage `TODO`).
- Cursor image jumps to (0,0) after all monitors wake from DPMS/sleep
  (`code/src/dwl.c`, `FIXME` in the output-config-apply path) — the fix
  attempted (`wlr_cursor_move(cursor, NULL, 0, 0)`) doesn't actually restore
  the prior position, per its own comment.
- Cursor's very first on-screen position at compositor startup is a hacky
  warp-to-last-position rather than a clean initial placement — minor,
  cosmetic, low priority.

## Already shipped (see [[ledger]])

[[infinite-canvas]], [[viewport]] [[pan]]/[[zoom]]/[[follow-mode]]/[[fit-all]],
[[buffer-scaling]], [[connection-graph]] (superseding [[column-layout]] +
[[anchored-window]]), [[directional-focus]], [[crop-mode]], [[focus-ring]],
[[off-screen-indicators]], [[persistence]], [[ipc-socket]],
[[foreign-toplevel]], multi-monitor, [[overview-mode]], [[window-menu]].
