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
	resize(c, geo, (c->isfloating && !c->isfullscreen));

	if (!c->isfloating && c->mon)
		arrange(c->mon);
}
