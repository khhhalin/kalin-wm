# Column layout

**SUPERSEDED — removed entirely.** kalin-wm no longer has a column layout, a
tiled/floating dichotomy, or any auto-arranging layout mode. Every window is
free-positioned at a persistent [[world-coordinates|world position]] that
nothing touches except the user or a [[connection-graph]] operation. See
[[connection-graph]] for the current model and the [[ledger]] for when/why
this was replaced.

The rest of this note describes the old design, kept for history:

- The column layout auto-placed windows in a horizontal strip of columns on the [[infinite-canvas]], Niri-style.
- It was the default layout (the `infinite` layout).
- A new window that was not an [[anchored-window]] was assigned to a column by [[window-placement]].
- [[move-column]] (`Ctrl+Left` / `Ctrl+Right`) moved the focused window between columns.
- Was implemented in `layout/layout_world.c`, since deleted.
