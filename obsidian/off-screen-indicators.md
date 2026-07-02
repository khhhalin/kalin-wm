# Off-screen indicators

Off-screen indicators are small edge markers that appear when windows exist
outside the [[viewport]] on the [[infinite-canvas]]. They point toward windows
that are currently off-screen.

They are tunable through `offscreen_indicator_*` options in
[[compile-time-config]] and implemented in the `ui/offscreen_indicators.c`
runtime module.

They help the user re-find windows after panning away, alongside [[fit-all]].
