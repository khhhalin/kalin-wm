# Overview Mode

> Zoom-out view of all windows.

## Status

**Decided and shipped** (2026-07-07). See [[overview-mode]] for the live behavior and
`code/src/modules/viewport/overview.c` for the implementation.

## Options considered

| Option | Description | Pros | Cons |
|--------|-------------|------|------|
| A | No overview | Simple | Hard to navigate many windows |
| B | Zoom-to-fit | Single action | No interaction |
| C | Full overview (Niri) | Interactive, draggable | Complex |
| D | Minimap corner | Always visible | Screen real estate |

## Decision

**B, but it turns out B and C converge here rather than trading off against each
other.** kalin-wm already has a real camera over a real 2D canvas (unlike niri's
workspace-strip model) — [[fit-all]] already *is* option B, and reusing it as a
toggle-that-remembers-and-restores gets essentially all of option C's interactivity
(click-to-focus, drag, at-any-zoom hit-testing) for free, since it's the same live
scene, not a re-flowed thumbnail grid. No separate renderer, no drag-and-drop to build
specially — dragging already works at any zoom level. Option D (minimap) stays a
separate, unrelated post-v1.0 idea (see [[roadmap]]).

---

*See [[index]] for other design documents.*
