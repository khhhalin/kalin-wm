# Rail

The **rail** is a 1D doubly-linked order that keyboard-spawned managed windows
flow into — the "hybrid rail on the free canvas" from [[layout-rethink]], built
in **layout Phase 2 (DONE 2026-08-13)**. It replaces the removed
[[connection-graph]] (8-octant neighbours) with one dimension and one default
rail: a window flows rightward from its spawn-parent, and closing one closes the
gap. The [[infinite-canvas]] + [[viewport]] camera and free 2D positioning are
untouched — the rail is *placement*, not a tiling straitjacket.

Code: `code/src/modules/layout/rail.c` (separate TU, like [[directional-focus]]),
`mapnotify()`/`unmapnotify()` in `code/src/dwl.c`, binds in
`code/config/default_binds.h`. Unit-tested in `code/tests/test_rail.c`.

## Data structure

- `Client *rail_prev, *rail_next` (`code/include/kalin.h`) — the linkage.
  `rail_prev == rail_next == NULL` means the window is **not on the rail**
  (free / [[floating-windows|float]] / future overlay bucket): panels, floating
  overlays, and any window never keyboard-spawned stay free-positioned. This is
  deliberate — not everything is forced onto the rail.
- `Client *rail_head` (global in `dwl.c`, externed in `kalin.h`) — the leftmost
  member, anchoring iteration/scroll. Kept correct on every insert/close/swap;
  `NULL` when the rail is empty.
- Rejected `int rail_index` (renumbering churn on every insert; can't express
  off-rail) — pointers, mirroring the discipline the old graph used, but 1D.

## Placement (`mapnotify`)

A keyboard-spawned managed window lands `SPAWN_GAP` px to the right of its
focused spawn-parent `p`, at **`p`'s baseline y** (`p->geom.y`), then:

1. `rail_open_gap_after(p)` — a forward walk over `p->rail_next` shifting every
   successor right by the new window's width + gap (the 1D heir of the old
   graph's `collect_component` push). Done **before** linking `c` in, so the walk
   sees the old successors.
2. `rail_insert_after(p, c)` — splice `c` after `p`. Edge cases keep the *one
   default rail* a single chain: no parent → `c` seeds the rail; parent not yet
   on a rail → parent seeds it as head and `c` follows (first keyboard spawn +
   its parent both join, in spawn order); a parent off an existing rail →
   append to the tail rather than fork.

Panels (`c->ispanel`) and floating windows never reach this branch — they're
handled by the `isfloating`/dockprep branches above it. The dockprep,
persistence-restore, cursor-center and monitor-center fallbacks are all intact.

## Gap-close (`unmapnotify`)

`rail_remove(c)`: shift `c`'s successors left by its footprint (width + gap,
niri-style), then splice `rail_prev`↔`rail_next`, fix `rail_head` if `c` was the
head, and **null `c->rail_prev/rail_next`** (a dangling `rail_next` after a close
is the classic use-after-free — nulled like `unmapnotify` nulls other per-client
refs). No-op for a window that was never on the rail.

## 1D swap — `Super+Ctrl+Left/Right`

`rail_swap_dir(dir)` (`ACT_RAIL_SWAP`): for LEFT/RIGHT, trade the focused
window's **screen position** (animated via the spring, `client_set_target_geom`,
as the old graph swap did) **and** its **rail linkage** with its rail neighbour;
keeps the focused window framed (`viewport_follow_focus()`). No-op at the ends
and for UP/DOWN (the rail has no vertical order). `Super+Ctrl+Up`/`Down` are
left unbound.

## Discrete scroll — `Super+Ctrl+h/l`

`rail_focus_dir(dir)` (`ACT_RAIL_FOCUS`): focus the prev/next rail neighbour and
frame it with an explicit camera jump (`viewport_center_on`). This is the
"snap to the next/prev rail window" companion to the loose free camera pan
(`viewport.pan`, `Super+scroll` / `Super+Shift+arrow`) — both kept, per the
signed-off "rail scroll = both" sub-decision in [[layout-impl]].

## What's NOT done yet (later phases — [[layout-impl]])

- **Phase 3** — growing a window pushes the rail (re-hook a 1D forward-walk over
  `rail_next`; `allow_overlap` / `Super+Shift+o` re-homes here as "grow over
  successors instead of pushing").
- **Phase 4** — float-under-cursor (menu spawns), the one missing primitive
  (needs a shell→compositor `float-next` hint).
- **Phase 5** — attached overlay (child→host follow), reusing the freed
  `Super+L`.
- **Phase 6** — **rail-order persistence.** NOT folded into Phase 2: the rail is
  rebuilt only from live spawns, so a restart currently restores window
  positions/sizes/crop/opacity/camera (unchanged) but **not** rail linkage.
  Phase 6 adds that via the existing appid/title/instance identity keys.

See also: [[layout-rethink]] · [[layout-impl]] · [[connection-graph]] ·
[[window-placement]] · [[spawn]] · [[keybindings]] · [[directional-focus]]
