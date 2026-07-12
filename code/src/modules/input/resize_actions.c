/* Keyboard-driven window resize actions.
 *
 * Separately-compiled translation unit: pulls the shared data model, globals,
 * and prototypes from kalin.h (without DWL_INTERNAL, so it sees the extern
 * interface that dwl.c backs). */
#include "kalin.h"

void
resizefocused(const Arg *arg)
{
	Client *c;
	struct wlr_box geo;
	const int *delta;

	if (!arg || !arg->v || !selmon)
		return;

	c = focustop(selmon);
	if (!c || c->isfullscreen)
		return;

	delta = (const int *)arg->v;
	geo = c->geom;
	geo.width += delta[0];
	geo.height += delta[1];

	/* Keep top-left anchored for keyboard resizing. */
	resize(c, geo, 0);
}

/* Stretch the focused window to the monitor's usable width, growing/
 * shrinking evenly on both sides so the window's horizontal *center* stays
 * put (not anchored at the left edge, which read as "grows to the right
 * only" — the previous behavior) — "fit to screen width" without the old
 * toggle-maximized's jankiness (no premax snapshot/restore state machine,
 * just a direct one-shot resize; pressing it again is idempotent). Windows
 * keep a persistent absolute *world* position on this infinite canvas
 * (mon->w.x/y is only ever a placement anchor for a brand new, parent-less
 * window in mapnotify() — never a bound this resize should snap existing
 * windows back to): an earlier version set geo.x = c->mon->w.x, which
 * teleported the window to the monitor's fixed home offset in world space
 * regardless of where it currently sat in a connection-graph chain,
 * abandoning its neighbors and overlapping whatever else happened to live
 * near that anchor — this is what broke the tiling. Growing in place (from
 * either edge) can still newly overlap a connection-graph neighbor once the
 * window is wider than the gap to it, so push any such neighbor (and
 * everything still transitively connected beyond it) out of the way the
 * same way a client's own post-map growth does (see
 * resolve_growth_overlap(), dwl.c). Also re-centers the camera on the
 * window along the X axis only (viewport_center_on_x()) — the resize keeps
 * the window's own center fixed in world space, but the camera might not be
 * looking at that point, so this brings it into view without touching
 * wherever the camera was panned to vertically. */
void
fitwidth(const Arg *arg)
{
	Client *c;
	struct wlr_box geo;
	int old_width;
	(void)arg;

	if (!selmon)
		return;
	c = focustop(selmon);
	if (!c || c->isfullscreen)
		return;

	geo = c->geom;
	old_width = geo.width;
	geo.width = c->mon->w.width;
	geo.x -= (geo.width - old_width) / 2;
	resize(c, geo, 0);
	resolve_growth_overlap(c);
	viewport_center_on_x(c);
	status_mark_dirty();
	persistence_save();
}

/* Stretch the focused window to the monitor's usable height, growing/
 * shrinking evenly on both sides so the vertical center stays put — the
 * height counterpart of fitwidth() above (see its comment for why world
 * position must never be reset to the monitor's anchor, for the
 * evenly-both-sides anchor choice, and for why this re-centers the camera
 * on just the Y axis via viewport_center_on_y()). */
void
fitheight(const Arg *arg)
{
	Client *c;
	struct wlr_box geo;
	int old_height;
	(void)arg;

	if (!selmon)
		return;
	c = focustop(selmon);
	if (!c || c->isfullscreen)
		return;

	geo = c->geom;
	old_height = geo.height;
	geo.height = c->mon->w.height;
	geo.y -= (geo.height - old_height) / 2;
	resize(c, geo, 0);
	resolve_growth_overlap(c);
	viewport_center_on_y(c);
	status_mark_dirty();
	persistence_save();
}
