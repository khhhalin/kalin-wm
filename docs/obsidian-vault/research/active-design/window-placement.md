# Window Placement

> How new windows are placed in the infinite canvas.

## Decision

**Niri-style horizontal strip** with option to anchor in 2D space.

## Column-Based Placement

### Algorithm

1. Find the rightmost edge of all column windows
2. Add fixed column gap
3. Place new window there
4. Window becomes a new column

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

### Spawn Behavior (Current)

| Action | Keybind |
|--------|---------|
| Spawn terminal (new column by default) | `Super+T` |

## Anchored Placement

When user anchors a window, it:
- Detaches from column flow
- Keeps current world coordinates
- Keeps freeform world placement

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

## Implementation Note

Runtime behavior is defined in code paths under `place_window_column()` and `arrange_columns()`.

## Related

- [[layout-paradigm]] - Column vs anchored windows
- [[navigation]] - Moving between windows
- [[../comparators/niri-layout#Window Placement]] - Niri's approach
