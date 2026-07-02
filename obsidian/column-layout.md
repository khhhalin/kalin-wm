# Column layout

The column layout auto-places windows in a horizontal strip of columns on the
[[infinite-canvas]], Niri-style. It is the default layout (the `infinite`
layout); see [[niri-layout]] for the inspiration.

A new window that is not an [[anchored-window]] is assigned to a column by
[[window-placement]]. The layout computes each window's [[world-coordinates]]
from its column index.

[[move-column]] (`Ctrl+Left` / `Ctrl+Right`) moves the focused window between
columns. The layout is implemented in the `layout/layout_world.c` runtime module.

The column-layout placement and viewport-navigation choices are recorded as
[[layout-column|architecture decisions]].
