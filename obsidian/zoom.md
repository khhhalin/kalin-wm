# Zoom

**Status: parked (2026-07-01).** Zoom works but is dropped from active focus — the
interaction is being rethought and no zoom work is planned for now. Current effort
is on [[stability]], the [[quickshell-shell]], the scrolling [[column-layout]], and
the [[window-menu]]. The behaviour below describes what exists today.

Zoom scales the [[viewport]] in and out over the [[infinite-canvas]]. It is bound
to `Super+equal` (in, ×1.1) and `Super+minus` (out, ×0.9).

Zoom is a true scene scale: it scales the actual [[scene-graph]] render, so
content stays crisp rather than being a bitmap magnification. Zoom changes are
animated and eased.

The [[buffer-scaling]] system gives each surface a destination size that matches
the current zoom, so clients render at the right resolution.

[[fit-all]] picks the zoom level needed to frame every window at once. The zoom
divisor is guarded so it can never reach zero.
