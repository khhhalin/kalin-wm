# Active Design

> Active design specifications for kalin-wm.

## User Decisions

Based on architectural review, the following decisions have been made:

### 1. Layout Paradigm
**Hybrid** - Column-based primary with 2D anchoring option.

See: [[layout-paradigm]]

### 2. Window Placement
**Horizontal strip** like Niri, with ability to anchor windows in 2D space.

See: [[window-placement]]

### 3. Navigation
- **Super + Arrows** → Directional focus
- **Super + Shift + Arrows** → Camera pan

See: [[navigation]]

### 4. Overview Mode
**Deferred** - Decision postponed until implementation phase.

## Design Documents

| Document | Purpose | Status |
|----------|---------|--------|
| [[layout-paradigm]] | Column vs anchored windows | Decided |
| [[navigation]] | Keyboard navigation scheme | Decided |
| [[window-placement]] | New window placement | Decided |
| [[crop-true-clipping-plan]] | Crop behavior rework (true clipping) | Planned |
| [[overview-mode]] | Zoom-out overview (TBD) | Deferred |
| [[animations]] | Smooth transitions | Draft |

## Exit Conditions

See [[exit-conditions]] for definition of done.

## Research Basis

These designs are informed by:

- [[../comparators/niri-layout]] - Column-based scrolling
- [[../comparators/driftwm]] - 2D infinite canvas
- [[../comparators/zoom-features]] - Overview modes

---

*Last updated: 2026-04-20*
