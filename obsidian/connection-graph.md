# Connection graph

- **Current model, replacing the old [[column-layout]]/[[anchored-window]]
  split entirely.** Every window is free-positioned: it has one persistent
  absolute [[world-coordinates|world position]] (`Client.geom.x/y/width/
  height`) that nothing auto-repositions except the user (drag) or one of the
  graph operations below. There is no tiled/floating dichotomy and no column
  index any more.
- A **connection** is an undirected edge between two windows recorded as up
  to 8 neighbor slots per window, `Client.neighbor[8]`, one per compass
  octant (`enum Octant { OCT_N, OCT_NE, OCT_E, OCT_SE, OCT_S, OCT_SW, OCT_W,
  OCT_NW }` in `kalin.h`). A window has at most one neighbor per octant.
  Opposite octants are always `(oct+4)%8`.

**Lives in `code/src/modules/layout/connection_graph.c`** (extracted from
`dwl.c` 2026-07-09, along with `directional_focus.c` and `client_anim.c` —
part of the ongoing dwl.c-shrinking effort tracked in [[roadmap]]; see the
[[ledger]] for the full sweep). `swap_neighbor_dir()` moved here too, since
it's fundamentally graph-topology manipulation, not the geometric cone-search
`focus_directional()` is (that one's its own module, `directional_focus.c`).

## Forming and breaking connections

- `connect_clients(Client *a, Client *b)`: computes the
  octant from the real angle between the two windows' centers
  (`octant_from_delta()`, snapped to the nearest 45°) and links them
  symmetrically. **No-ops if either side's relevant slot is already
  occupied** — this single guard is the source of a couple of real bugs this
  project hit (see the [[ledger]]): callers that forget a stale back-pointer
  is still set silently fail to link.
- `sever_connection(id_a, id_b)`: clears whichever slot(s) reference each
  other. Driven by the IPC `sever <a> <b>` command and by clicking a
  connection line in the shell (compositor-side hit-testing,
  `connection_click_hit()` — a shell-side click mask covering only the line
  was tried first and abandoned; see the [[ledger]] for why).

## What the graph is for

- **[[spawn]] placement.** A new window's spawn-parent is whichever window
  was focused right before it was created; it's placed to that parent's
  right (`SPAWN_GAP` px) and connected. If the parent already has an East
  neighbor (inserting into the middle of an existing line), the new window
  is spliced in between instead of silently failing to connect: sever
  parent↔old-neighbor, shift old-neighbor and everything still transitively
  connected beyond it (`collect_component()`, a BFS over all 8 slots) right
  to make room, then connect parent↔new↔old-neighbor.
- **Group drag.** Dragging any window (`Super+BTN_LEFT`) drags its whole
  connected component (`collect_component()` from the grabbed window)
  together as a rigid group.
- **Solo drag (2026-07-13).** `Super+Ctrl+BTN_LEFT` on a window (`CurMoveSolo`,
  `moveresize()`/`motionnotify()` in dwl.c) moves just that window — same
  offset math as a normal drag, but the component-glide block is skipped, so
  the rest of the graph stays put and the connection just stretches to the
  new distance. Doesn't sever anything (no `sever_connection()` call on this
  path at all); it's purely "move without dragging neighbors," not "detach."
  On empty canvas the same chord still does its original job, direct-
  manipulation camera pan (`ACT_VIEWPORT_PAN_GRAB` in `bind_invoke()` tries
  the solo-move grab first and falls back to pan if nothing's under the
  cursor).
- **Directional swap.** `Super+Ctrl+Arrow` (`swap_neighbor_dir()`) trades
  the focused window's position with its neighbor in that direction (spring-
  glide animated, not an instant snap), flipping their connection's slot
  assignment since their relative direction inverted. Conflict resolution
  when the swap causes an octant clash: **transfer**, not evict — whichever
  of the two swapped windows moved into the vacated adjacent position
  inherits that position's old connection, instead of the third connection
  being silently dropped. Distinct from `Super+Arrow` (`focus_directional()`,
  unchanged, pure geometric cone-search — no graph involved).
  - **Bug fixed 2026-07-09 (found by code audit)**: the swap-axis handling
    above (oct/opp, the direct pair and the one "third" collinear client)
    was the *only* thing repaired — any neighbor of either window in a
    *different* direction kept its pre-swap slot, even though the window
    it pointed at had just physically moved. Concretely: a plus-shaped
    layout (C has N/E/S/W neighbors), swap C left with W — C's N/S links
    used to stay stale, pointing at windows no longer actually N/S of C's
    new position, so a later directional-focus/swap through that slot went
    to the wrong window and the drawn connection line contradicted the
    actual layout. Fixed by swapping *every* other-axis neighbor slot
    between the pair too (safe without collision risk, since it's always
    the same index i on both sides — unlike the oct/opp transfer, which can
    land in a different, possibly-occupied slot). Regression test:
    `test_swap_preserves_off_axis_neighbors`, `code/tests/test_connection_graph.c`.
- **Closing a window closes the gap it leaves.** `unmapnotify()`'s cleanup
  first splices each pair of opposite neighbors (N↔S, NE↔SW, E↔W, SE↔NW)
  directly together if both sides of that axis exist, so removing the middle
  of a line reconnects what's left into one line instead of leaving two
  dangling ends — then shifts the far side's whole component
  (`close_gap()`) so the facing edge-to-edge distance becomes exactly
  `SPAWN_GAP` again, instead of leaving a visual hole where the closed
  window used to be. Both steps had to clear the *closing* window's stale
  back-pointers first — `connect_clients()`'s occupied-slot guard otherwise
  refuses the splice every time, since from its perspective the slot still
  points at the client that's mid-teardown.
- **Growth after placement.** A window can grow *after* it was already
  positioned relative to its neighbors — most visibly an Electron/GTK app
  (Obsidian was the reproduction case) that maps at a small placeholder size
  and settles into its real, much larger size on a later commit, or the
  `fitwidth()`/`fitheight()` keybind (`Super+F`/`Super+Shift+F`) stretching a
  window to the monitor's usable width/height in place. `resolve_growth_
  overlap(Client *c)` runs after any such growth: for each of the 4 primary
  directions, if `c`'s new edge would now overlap that neighbor, push the
  neighbor (and everything still transitively connected beyond it) further
  away by the overlap amount — the same `collect_component()`-based shift
  the insert-placement path above uses. Wired into `commitnotify()` (client-
  initiated growth) and explicitly called from `fitwidth()`/`fitheight()`
  (see the [[ledger]] — `fitwidth()` used to reset the window's world `x` to
  the monitor's anchor instead of growing in place, "breaking the tiling"
  for any window not near that anchor; fixed to leave position alone and
  only touch size).
- **Opt-out: `allow_overlap`** (2026-07-09, `Client.allow_overlap`, toggled by
  `Super+Shift+O` / `toggle-overlap` / the hold-Super menu's "Overlap"
  button, see [[window-menu]]). When set, `resolve_growth_overlap(c)` returns
  immediately for that client — its growth never pushes neighbors aside, so
  it's free to sit on top of them instead. Purely a flag; no persistence,
  no interaction with the insert-splice path (spawn-time placement still
  avoids overlap even for a client that already has the flag set from a
  previous session — moot today since the flag isn't persisted).
- **IPC broadcast** (`code/src/modules/ipc.c`): every live edge, screen-
  transformed, always sent (not gated on Super being held) —
  `"connections":[{"a":id,"b":id,"a_rect":{...},"b_rect":{...}}]`, deduped by
  only emitting from the lower-id side. The [[quickshell-shell]] draws these
  as dotted/sparkle lines while Super is held **or** [[overview-mode]] is
  open (`ConnectionLines.qml`'s visibility condition); drawing is purely
  decorative, all hit-testing is compositor-side (see above). Each line's
  endpoints are pulled in from the window's edge toward its center by a
  fixed inset (`LineGeometry.qml`'s `_edgeAnchor()`), not placed exactly on
  the boundary — keeps the line's origin visually distinct from a
  neighboring window's edge when the two are close or slightly overlapping.

## Manual create/sever (2026-07-10)

Both gestures below exist because the only way to form a connection used to
be automatic (spawn-adjacency, splice-on-insert/close), and severing
required a pixel-precise click — both reported as friction. Researched how
node-graph editors handle the same "draw/cut a link" problem (no mainstream
window manager has an equivalent — niri/PaperWM are scrollable-tiling, not a
persistent adjacency graph): Houdini and Unreal Blueprint both use drag-to-
wire for creating and hold+drag-across-to-cut for severing, rather than a
precise click for either.

- **Sever: drag-to-cut sweep.** `buttonpress()` used to test
  `connection_click_hit()` once, at the initial press point. It now enters a
  new cursor mode, `CurCut` (`kalin.h`), on a Super+LMB press over empty
  canvas; `motionnotify()` re-runs the same hit-test at the *current* cursor
  position every tick for the rest of the drag, severing whatever comes
  within `CONN_HIT_RADIUS_PX`. A plain click still works unchanged (the
  zero-motion case of the same code path) — this only makes a *deliberate*
  drag also forgiving, sweeping the cursor near/across a line to cut it
  instead of needing to land exactly on it.
- **Create: menu-armed pending connect.** A literal QML drag handle on
  `WindowActions.qml`'s menu isn't viable — that overlay is deliberately
  click-through (`mask: Region {}`), and `ConnectionLines.qml` already
  documents why a partial input region for lines never received clicks on
  this wlroots/Quickshell combination. Instead: `Super+L` (`link-pick` /
  `ACT_LINK_PICK`, the menu's new "Link" button, see [[window-menu]]) calls
  `connect_pick_arm()` (`connection_graph.c`), which sets a file-static
  `pending_connect_source` to the focused window. While armed:
  - The IPC state gains `"pending_connect":{rect,cursor}|null`
    (`ipc_build_state()`) — the source's screen rect plus the live cursor
    position, updated every `motionnotify()` tick via `status_mark_dirty()`
    (gated on `connect_pick_pending()` being non-null, so this adds no
    per-frame IPC traffic in the common case).
  - `ConnectionLines.qml` draws this as one more dotted/sparkle line (same
    `LineGeometry.hitPoints()` used for real connections — a zero-size rect
    at the cursor position degenerates its edge-anchor math to a point, so
    no changes were needed there) from the source to the cursor.
  - `buttonpress()`: a click on a *different* window while armed calls
    `connect_pick_complete(target)`, which defers to the existing
    `connect_clients()` (already no-ops on an occupied slot or an existing
    link, so no extra guard needed) and clears the pending state. A click on
    empty canvas, or releasing Super (`keyboard.c`), cancels instead. The
    armed client closing (`unmapnotify()`) also cancels, so the pending
    pointer never dangles.
- Both are compositor-side state machines, not independently unit-testable
  the way `connect_clients()`'s pure math is — `connect_pick_complete()`'s
  *guard logic* (no-op if nothing armed, no-op if target is the source) is
  reimplemented verbatim in `test_connection_graph.c` per the project's
  usual pattern for this file; the drag/motion dispatch itself was verified
  in the [[test-vm]] instead (arm → live line follows cursor → click
  completes → menu's on-state clears; drag-cut severs an existing line
  without landing precisely on it).

## Persistence

- The graph is saved and restored across restarts too, not just position —
  see [[persistence]].
