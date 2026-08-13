# Layout implementation plan — rail + overlay-attach

**Status: IN PROGRESS. Phase 0 + Phase 1 + Phase 2 DONE (2026-08-13); Phases 3–6 pending.**
Implements the decided model in [[layout-rethink]]. Code-grounded (file:line from a
planning pass; line numbers drift as phases land). See [[parallel-tracks]].

## Findings that shrink the scope

Three of the four "new capabilities" in [[layout-rethink]] are **already built**:

- **Per-window opacity exists** — `Client.opacity` (`kalin.h:352`, 0.1..1.0),
  `setopacity`/`opacityadjust` (`dwl.c:3415-3444`), `ACT_OPACITY` bind, `wlr_alpha_modifier_v1`,
  applied per-buffer in `commitnotify` (`dwl.c:1152`). **Only gap: not persisted.**
- **Crop exists** and is graph-independent — renders off `c->geom`, so it follows a moving
  child for free. No changes needed.
- **directional-focus** (`modules/layout/directional_focus.c`) is pure cone-search geometry,
  never reads `neighbor[]` — survives graph removal untouched.

So the real work is: remove the graph, add a rail order, add overlay-attach, route
menu-spawns to float-under-cursor, extend persistence. **The architectural key:** every
window position flows through one funnel — `resize(c, geom, interact)` (`dwl.c:2855`);
`arrange()` no longer positions anything. So overlay-follow is **one hook in `resize()`**
that covers *all* move sources (drag, rail swap, gap-close, setmon, restore).

## Data structures (proposed)

```
// rail: 1D doubly-linked order, mirroring the neighbor[] pointer-discipline the
// codebase already knows, but one dimension. NULL = not on the rail (free/float).
Client *rail_prev, *rail_next;        // + global Client *rail_head in dwl.c
// overlay-attach: directed child -> host + world-space offset. child follows host.
Client *overlay_host;                 // NULL = not an overlay
int overlay_off_x, overlay_off_y;
```
Rejected: `int rail_index` (renumbering on every insert; can't express "off-rail").

## Phases (each keeps the build green)

The graph can be removed **before** the rail exists — placement falls back to the existing
cursor/monitor-center branches (`dwl.c:2135-2149`), so windows still spawn sanely. No atomic
swap needed.

- ~~**Phase 0 — Persist opacity**~~ **DONE 2026-08-13** (commit merged to local main).
  `SavedClientState.opacity` + parser (1.0 fallback) + `save_client_cb` write + restore-block
  apply (set `c->opacity` directly, clamped 0.1..1.0; `commitnotify`'s `applyopacity` reapplies).
  Build clean, test 25/25, SAVE round-trip verified on a live nested build. Note: `setopacity()`
  turned out to be dwl.c-internal, so the field is set directly rather than via that call.
- **Phase 1 — Remove the connection-graph** — **DONE 2026-08-13** (branch
  `layout-phase1-remove-graph`). Deleted `connection_graph.c` + its unit test,
  `Client.neighbor[8]`, `enum Octant`, the `CurCut` cursor mode, every caller
  (Super+L link-pick, click/drag-sever, cross-monitor sever, group-drag,
  `unmapnotify` splice/gap-close, `commitnotify`/`fitwidth`/`fitheight`
  growth-push, the mapnotify insert-splice), the IPC `connections` broadcast +
  `pending_connect` field + `sever` command, and connection persistence
  (`save_connections`/reconnect, `SavedConnection`). Keyboard spawn now just
  places right-of-parent at the same y (no rail yet); close leaves a hole;
  drag moves only the grabbed window. `allow_overlap` (`Super+Shift+o`) kept as
  a dormant flag (toggles + broadcasts, does nothing until Phase 3);
  directional-focus untouched and still works. **`ACT_SWAP_DIR`/`swap-dir`
  removed cleanly** (enum entry, string table, parser case, bind_invoke case,
  and the four `Super+Ctrl+Arrow` default binds) — freeing that chord for the
  Phase 2 rail 1D-swap; `Super+L`/`link-pick` likewise removed, freeing it for
  the Phase 5 overlay-pin. Build clean (only the pre-existing
  `default_binds.h` overlength-strings warning), tests 25/25 + shader-math OK,
  nested smoke-boot verified. **Cross-repo follow-up DONE 2026-08-13**
  (`~/environment/kalin-shell` commit `5788f49`): deleted
  `ConnectionLines.qml`/`LineGeometry.qml` (+ qmldir entry + shell.qml
  instantiation), removed KalinViewport's `connections`/`pendingConnect`/`sever`
  and the WindowActions "Swap"/"Link" hints; kept pan/zoom/follow/dockPrep. The
  shell now loads clean against both old and new compositor (verified: quickshell
  reload, 0 QML errors, 0 restarts) → **Phase 1 is deploy-safe.**
- **Phase 2 — Rail** — **DONE 2026-08-13** (branch `layout-phase2-rail`). Added
  `Client.rail_prev/rail_next` + global `rail_head`; new module
  `code/src/modules/layout/rail.c` (separate TU) with `rail_insert_after` /
  `rail_open_gap_after` / `rail_remove` / `rail_swap_dir` / `rail_focus_dir`.
  `mapnotify` places a keyboard-spawn right of its focused parent at the
  parent's baseline y, shifts successors right (`rail_open_gap_after`), then
  splices (`rail_insert_after`); `unmapnotify` gap-closes + unlinks
  (`rail_remove`, nulls the pointers). **Chords:** 1D swap on the freed
  **`Super+Ctrl+Left/Right`** (`rail-swap`; `Up`/`Down` left **unbound** — the
  rail is 1D); discrete "focus+frame next/prev rail window" on
  **`Super+Ctrl+h/l`** (`rail-focus`, reuses `viewport_center_on`), the snap
  companion to the kept loose free pan. Wired the two new actions through the
  bind DSL (enum/string-table/parser/dispatch). Build clean (only the
  pre-existing `default_binds.h` overlength warning); tests 25 binds + 10 new
  `test_rail` linkage cases + shader-math, 0 failures; nested smoke verified —
  window 2 landed right of window 1 (x 340→1060), window 3 right of 2
  (1060→1780), closing the middle gap-closed the successor left by 722, no
  crash/asserts. **Deferred to Phase 6:** rail-order **persistence** — the rail
  rebuilds from live spawns only; a restart restores positions/size/crop/opacity/
  camera (unchanged) but not rail linkage. As-built note: [[rail]].
- **Phase 3 — Growing pushes the rail** (reintroduce the dropped `resolve_growth_overlap` as a
  1D forward-walk over `rail_next`; re-hook `commitnotify:1148` + `resize_actions.c`).
- **Phase 4 — Float-under-cursor** (menu spawns). **The one genuinely missing primitive:** no
  compositor signal distinguishes keyboard vs menu spawn (both arrive via the tmux `spawn` path).
  Needs a shell→compositor pre-spawn hint — recommend a dockprep-style `float-next <appid>` IPC
  consumed in `mapnotify` (mirrors `dockprep_consume`). Placement reuses the cursor-center branch
  (`dwl.c:2135`) + a non-obscuring offset rule (to spec).
- **Phase 5 — Attached overlay** (child→host follow). Add the fields + a "pin to host" entry
  (IPC + WindowActions action, reuse the freed Super+L). The follow is **one hook at the end of
  `resize()`**: walk `clients` for `overlay_host == c`, resize each to `host.origin + offset`
  (guard re-entrancy). Crop+opacity already apply off the child's geom → the Discord-on-Minecraft
  case works with no crop/opacity code changes. Exclude overlays from rail + directional-focus.
- **Phase 6 — Persist rail + overlay** (drop `connections`; add rail linkage + overlay host-key +
  offset via the existing appid/title/instance identity keys; rebuild order-independently on load).

## Resolved sub-decisions (signed off 2026-08-13)

1. **Spawn-source signal** (Phase 4) → **dockprep-style `float-next <appid>`**: the menu arms
   the hint before spawning, `mapnotify` consumes it (mirrors `dockprep_consume`). Default =
   rail; float is the exception. No map-in-rail-then-move flash.
2. **Rail scroll** → **both**: keep free camera pan (`viewport.pan`, exists) for loose
   continuous scroll, **plus** a new discrete action "focus + frame next/prev rail window"
   reusing `viewport_center_on()`. Loose by default, snap on demand — not forced.
3. **`allow_overlap`** (Super+Shift+o, `toggle-overlap`) → **keep, re-homed into Phase 3**: a
   flagged window grows **over** its rail successors instead of pushing them. Live bind,
   semantics still coherent under the rail.

### Still to spec at coding time (not blockers)
4. **Non-obscuring offset rule** — concrete geometry for float-under-cursor + overlay anchoring
   (Phase 4/5).
5. ~~**Cross-repo**~~ **DONE** — the kalin-shell `ConnectionLines`/`LineGeometry`/"Link"/"Swap"
   removal landed with Phase 1 (kalin-shell `5788f49`); shell verified loading clean against
   both old and new compositor. Phase 1 is deploy-safe.

## Critical files

`dwl.c` (mapnotify 2075-2167, unmapnotify 4110-4156, motionnotify 2400-2416, resize 2855,
opacity 3415-3444) · `modules/layout/connection_graph.c` (delete) · `kalin.h` (Client struct) ·
`persistence.c` (save_client_cb, save_connections, apply/reconnect) · `modules/ipc.c` (connections
broadcast, new float-next/overlay-pin commands) · `~/environment/kalin-shell` (ConnectionLines).
