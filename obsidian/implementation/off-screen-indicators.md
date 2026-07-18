# Off-screen indicators

- Off-screen indicators are small edge markers that appear when windows exist outside a monitor's [[viewport]] on the [[infinite-canvas]].
- They point toward windows that are currently off-screen.

- Per-monitor since [[multi-camera]] Phase 4 (2026-07-18): each monitor has its
  own four-marker set (lazily created, garbage-collected when its monitor is
  destroyed or disabled — the module can't hook monitor lifecycle because
  dwl.c owns it), showing only that monitor's *own* held windows (`c->mon`)
  tested against and clamped to its *own* box (`m->m`), not the whole layout
  (`sgeom`). The off-screen test scales the window's world size by the
  holder camera's zoom; camera-bypassed clients (fullscreen/maximized/docked)
  and panels are skipped — they're glued to the screen by construction.

- They are tunable through `offscreen_indicator_*` options in [[compile-time-config]].
- They are implemented in the `ui/offscreen_indicators.c` runtime module
  (`#include`'d into dwl.c, updated every frame from `rendermon()`;
  `offscreen_indicators_configure()` is now a kept-for-the-call-site no-op —
  positions derive from each `m->m` lazily).

- They help the user re-find windows after panning away, alongside [[fit-all]].
