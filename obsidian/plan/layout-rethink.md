# Layout rethink — scrolling rail

**Status: MODEL DECIDED (2026-08-13), implementation not yet planned.** The core model
(hybrid rail on the free canvas) and its behaviors are signed off; what remains is an
implementation plan, not a design decision. Third iteration of the core layout model,
keeper-level — this **supersedes [[connection-graph]]** for tiling once built. See
[[parallel-tracks]]. Raw vision + reasoning that led here is kept below the decision.

## DECIDED MODEL — hybrid rail on the free canvas

Keep the [[infinite-canvas]] + [[viewport]] camera (kalin-wm's identity vs [[niri]]);
simplify only *placement*. Four placement buckets — the model is really "where does a
window go?":

1. **Rail** — keyboard-spawned primaries. New window lands to the right of the current
   at a baseline y, forming a 1D row; scrolling the rail = panning the camera along it.
   **One default rail**; the free canvas still allows informal extra rows (pan up/down to
   another cluster). Loose alignment — windows keep their own sizes, can be nudged off
   baseline; focus moves the camera to frame, no rigid columns.
2. **Float** — menu-spawned (right-click). Spawns **under the cursor**, offset so it does
   not obscure what was clicked (corner at cursor, expand toward the roomier side; if it
   would cover the click target, offset by that rect).
3. **Attached overlay** *(NEW — replaces group-drag)* — a float **pinned to a host window
   at a relative offset** that **follows the host** when the host moves (in the rail or
   freely). Combines with **[[crop-mode]]** (exists) and **per-window opacity** (NEW, a
   slider). The motivating case: crop the Discord window, drop its opacity, pin it in a
   corner of Minecraft as a custom overlay; move Minecraft along the rail and the Discord
   overlay tracks it exactly at the offset you set. This is the *only* relational concept
   that survives — targeted (one directed child→host link + offset), not the general
   8-octant graph.
4. **Agent-hidden** — test windows off-layout/off-camera (see [[agent-observation]]).

### Rail behaviors (decided)

- `Super+Ctrl+←/→` → swap with the left/right neighbor **in the rail order** (1D).
- Closing a rail window → the rail **closes the gap** (shift left, niri-style).
- Growing a window → **pushes the rail**.
- **[[directional-focus]] stays** — cone-search geometry, independent of any graph.

### Dropped vs kept

- **Dropped:** the [[connection-graph]] entirely (8-octant neighbors, sever, gap-splice
  *as graph ops*, `ConnectionLines.qml`, connection [[persistence]]) and general
  group-drag. Big simplification — a 1D rail order + one overlay-attach link replace a
  2D graph.
- **Kept:** camera (pan/zoom/[[follow-mode]]/persistence), free 2D positioning,
  [[crop-mode]], [[directional-focus]]. Persistence now saves position/size + **rail
  order** + **overlay attachments** + **per-window opacity**.

### New capabilities to build

- **Per-window opacity** — a slider (via the WindowActions menu / an IPC command);
  compositor sets the surface's scene opacity. *Verify if any per-window opacity exists
  today; likely new.*
- **Overlay-attach** — a directed pin (child → host, relative offset) + follow-on-host-move.
- **Rail placement + scroll** — 1D flow + camera-along-rail; gap-close; 1D swap.

### To resolve at implementation-plan time (not design blockers)

- Exact rail data structure (linear list of window ids per rail vs positions on canvas).
- How "informal extra rows" are detected/navigated vs the one default rail.
- Offset rule specifics for float-under-cursor and overlay-attach anchoring.

---

## Origin — raw vision + reasoning (kept for provenance)

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
