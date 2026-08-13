# Layout rethink — scrolling rail

**Status: PROPOSED / exploratory (2026-08-13), NOT signed off.** Raw vision from a
working session plus open questions. This would be the **third iteration** of the core
layout model and is keeper-level — nothing here supersedes [[connection-graph]] until
Kalin decides. See [[parallel-tracks]].

## Kalin's raw vision (verbatim intent)

- Move **away from the [[connection-graph]] system**.
- "Usually just a rail is enough" — a **scrolling layout** (niri-like) as the primary
  arrangement. When switching between windows they **don't have to be perfectly
  aligned** — loose is fine.
- Windows **spawned from the keyboard** go into the **scrolling rail**.
- Windows **spawned from the right-click menu** spawn **under the cursor**, positioned
  **so they don't obscure** (what was clicked / what's underneath) — a floating escape
  hatch, not on the rail.

## Where this sits in the model's history

The core layout has now been reconsidered twice already (see [[roadmap]] and
[[kalin-wm]]'s goal note):

1. `column-layout` + `anchored-window` (removed)
2. → [[connection-graph]] (current: free 2D positions + up-to-8 octant neighbor links)
3. → **scrolling rail** (this note, proposed)

So this is not a tweak — it's replacing the current core. Real deletions implied:
`connection_graph.c`, the octant neighbor system, connection [[persistence]],
`ConnectionLines.qml`, and the group-drag / directional-swap / sever / gap-splice
behaviors the graph drives.

## Emerging theme: three placement buckets

The pieces of the last session's discussion cohere into **"where does a new window
go?"** having three answers:

- **rail** — keyboard-spawned, flows into the scrolling layout.
- **float-under-cursor** — menu-spawned, near the pointer, non-obscuring.
- **agent-hidden** — test windows, off-layout/off-camera (see [[agent-observation]]).

## Open questions (to resolve before any plan)

- **Keep the infinite 2D [[infinite-canvas]] + [[viewport]] camera, or go 1D like
  niri?** "Loose alignment" hints at a *hybrid*: a rail as the default arrangement on the
  still-free canvas (windows flow rightward but keep 2D freedom + zoom), rather than
  strict niri columns. Which?
- **What survives from the connection-graph?** Group-drag, directional swap
  (`Super+Ctrl+Arrow`), sever, gap-splice-on-close — drop, or re-home onto the rail?
- **Why build this vs just use [[niri]]?** niri already is a mature scrollable-tiling
  compositor and is Kalin's fallback VT. The honest question: what does kalin-wm's rail
  do that niri doesn't — i.e. what justifies re-implementing it here rather than the
  camera/curation features that make kalin-wm *his*? (Answer shapes the whole design.)
- **Interaction with [[persistence]]'s camera restore** — a 1D rail changes what "camera
  position" means to persist (rail scroll offset vs free 2D pan). Not wasted, but shifts.
- **`float-under-cursor` non-obscuring geometry** — offset from the cursor by which rule
  (below-right of the clicked point? avoid covering the clicked element's rect?).

See also: [[connection-graph]] · [[infinite-canvas]] · [[niri]] · [[parallel-tracks]] · [[roadmap]]
