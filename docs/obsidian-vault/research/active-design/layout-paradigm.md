# Layout Paradigm

> Hybrid layout system combining Niri-style columns with freeform 2D anchoring.

## Decision

**Hybrid Layout** - Column-based primary with 2D anchoring capability.

## Core Concept

```
┌──────────────────────────────────────────────────────────────┐
│                                                              │
│  ┌──────┐  ┌──────┐  ┌──────┐  ┌──────┐                     │
│  │  A   │  │  B   │  │  C   │  │  D   │     ← Columns       │
│  │      │  │      │  │      │  │      │       (auto-placed) │
│  └──────┘  └──────┘  └──────┘  └──────┘                     │
│                                                              │
│        ┌────┐                                               │
│        │ E  │  ← Floated/anchored window                     │
│        └────┘       (user positioned)                       │
│                                                              │
└──────────────────────────────────────────────────────────────┘
         ↑ visible area ↑
              ← scroll →
```

## Two Window Types

### 1. Column Windows (Tiled)
- Automatically placed in horizontal strip
- Stacked vertically within columns
- New windows open in next available column space
- Follow Niri's placement algorithm

### 2. Anchored Windows (Freeform)
- User-positioned anywhere in 2D space
- Detached from column flow
- Remember their world coordinates
- Can be "re-columnized" to return to strip

## State Diagram

```
┌─────────────┐     anchor      ┌─────────────┐
│   Column    │ ──────────────-> │  Anchored   │
│   (tiled)   │                 │ (freeform)  │
└─────────────┘ <-────────────── └─────────────┘
     ▲              re-column
     │
     │   new window
     └──────────────┐
                    │
              ┌─────┴─────┐
              │  Spawn    │
              └───────────┘
```

## Keybinds

| Action | Keybind |
|--------|---------|
| Anchor focused window | `Super+Shift+A` |
| Re-columnize window | `Super+Shift+C` |

> Note: keyboard movement for anchored windows is planned but not implemented yet.

## Related

- [[window-placement]] - How new windows are placed
- [[navigation]] - How to navigate between windows
- [[../comparators/niri-layout]] - Niri's column approach
- [[../comparators/driftwm]] - Driftwm's freeform approach
