# Window Placement Decision

> How new windows are placed in the infinite canvas.

## Status

**Status:** DECIDED — Implemented

## Decision

**Niri-style horizontal strip** with option to anchor in 2D space.

## Column-Based Placement Algorithm

1. Find the rightmost edge of all column windows
2. Add fixed column gap
3. Place new window there
4. Window becomes new column

```
Time ──────────────────────────────────────────────->

Step 1: Open window A
┌──────┐
│  A   │
└──────┘

Step 2: Open window B
┌──────┐  ┌──────┐
│  A   │  │  B   │
└──────┘  └──────┘
          ↑
          rightmost + spacing

Step 3: Open window C
┌──────┐  ┌──────┐  ┌──────┐
│  A   │  │  B   │  │  C   │
└──────┘  └──────┘  └──────┘
                              ↑
                              rightmost + spacing
```

## Current Spawn Keybind

| Action | Keybind |
|--------|---------|
| Spawn terminal (new column by default) | `Super+T` |

## Anchored Placement

When user anchors a window:
- Detaches from column flow
- Keeps current world coordinates
- Can be moved freely in 2D space

```
Before anchor:
┌──────┐  ┌──────┐  ┌──────┐
│  A   │  │  B   │  │  C   │  ← All in strip
└──────┘  └──────┘  └──────┘

After anchor B:
┌──────┐  ┌──────┐
│  A   │  │  C   │     ← Strip continues
└──────┘  └──────┘
     
     ┌──────┐
     │  B   │          ← B anchored in 2D
     └──────┘            (user can move)
```

## Auto-Pan Behavior

When `follow_new_windows` is enabled:
- Camera pans to show new window
- Centers the window in viewport
- Smooth animation

## Configuration

```c
/* config.h */
/* Key behavior is configured through layout code and keybindings in config.h */
```

## Source of Truth

This file is the canonical placement decision record.

## Consequences

**Pros:**
- Predictable placement
- Logical left-to-right reading order
- Easy to find newest window

**Cons:**
- Can create very wide canvas
- Need scrolling to see all windows

## Related

- [Layout System](./layout-column.md) — Column vs anchored concept
- [Viewport Navigation](./viewport-navigation.md) — Moving between windows
- [Niri Placement](../../comparators/niri/placement.md) — Reference implementation
