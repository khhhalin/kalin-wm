# Layout System Decision

> Column-based layout with optional 2D anchoring.

## Status

**Status:** DECIDED — Implemented

## Context

Need to choose a window layout paradigm that balances:
- Predictable window placement
- User control over positioning
- Efficient navigation
- Implementation complexity

## Options Considered

### 1. Traditional Tiling (dwm-style)
- Windows fit within screen boundaries
- Automatic space division
- Too rigid, windows get too small

### 2. Scrollable-Tiling (Niri-style)
- Horizontal columns, scrollable canvas
- Predictable placement
- Limited to column strip

### 3. Infinite 2D Canvas (DriftWM-style)
- Freeform positioning anywhere
- Maximum flexibility
- No predictable placement

### 4. Hybrid (Chosen)
- Column-based primary layout
- Optional 2D anchoring for user positioning
- Best of both worlds

## Decision

**Hybrid Layout** — Column-based primary with 2D anchoring capability.

```
┌──────────────────────────────────────────────┐
│                                              │
│  ┌──────┐  ┌──────┐  ┌──────┐  ┌──────┐     │
│  │  A   │  │  B   │  │  C   │  │  D   │     │  Columns
│  │      │  │      │  │      │  │      │     │  (auto-placed)
│  └──────┘  └──────┘  └──────┘  └──────┘     │
│                                              │
│        ┌────┐                                │
│        │ E  │  ← Anchored window             │
│        └────┘     (user positioned)          │
│                                              │
└──────────────────────────────────────────────┘
         ↑ viewport ↑
              ← scroll →
```

## Two Window Types

### Column Windows (Tiled)
- Automatically placed in horizontal strip
- Stacked vertically within columns
- New windows open in rightmost column

### Anchored Windows (Freeform)
- User-positioned anywhere in 2D space
- Detached from column flow
- Remember world coordinates

## Window State Transitions

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
              │   Spawn   │
              └───────────┘
```

## Keybinds

| Action | Keybind |
|--------|---------|
| Anchor focused window | `Super+Shift+A` |
| Re-columnize window | `Super+Shift+C` |
| Move anchored window | `Super+Shift+Arrows` |

## Consequences

**Pros:**
- Predictable placement for most windows
- Flexibility when needed
- Clean mental model

**Cons:**
- Two different window behaviors to understand
- More complex implementation

## Related

- [Window Placement](./window-placement.md) — New window placement rules
- [Navigation](./viewport-navigation.md) — Moving between windows
- [Niri Layout](../../comparators/niri/) — Reference implementation
