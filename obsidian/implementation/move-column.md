# Move column

**SUPERSEDED — removed.** No columns exist any more (see [[column-layout]]).
The nearest current equivalent is `Super+Ctrl+Arrow`
(`swap_neighbor_dir()`), which trades the focused window's position with its
[[connection-graph]] neighbor in that direction — see [[connection-graph]]
and [[keybindings]]. It is a swap, not a reflow into a column; the
`Ctrl+Left`/`Ctrl+Right` binding itself is gone (freed for other use, no
conflict with the current `Super+Ctrl+Arrow` swap since it's a different
chord).

The rest of this note describes the old design, kept for history:

- Move-column moved the focused window between columns of the [[column-layout]], bound to `Ctrl+Left`/`Ctrl+Right`.
- Moving a window across columns reassigned its column index and the layout recomputed its [[world-coordinates]].
