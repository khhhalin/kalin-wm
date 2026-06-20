/* Keyboard-driven window resize actions. */
/* MOVED: see modules/input/resize_actions.c */

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
	resize(c, geo, (c->isfloating && !c->isfullscreen));

	if (!c->isfloating && c->mon)
		arrange(c->mon);
}
