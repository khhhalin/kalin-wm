/*
 * Traditional tiling layout for kalin-wm.
 *
 * This layout divides the screen into two areas:
 * - Master area (left): Contains nmaster windows stacked vertically
 * - Stack area (right): Contains remaining windows stacked vertically
 *
 * The master area size is controlled by mfact (master factor).
 * Windows are arranged in order of the clients list.
 */

#include "../../include/layout.h"

/* External dependencies from dwl.c */
extern struct wl_list clients;
extern void resize(Client *c, struct wlr_box geo, int interact);

/* Macro from dwl.c */
#define VISIBLEON(C, M) ((M) && (C)->mon == (M) && ((C)->tags & (M)->tagset[(M)->seltags]))
#define LENGTH(X) (sizeof X / sizeof X[0])
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#include <math.h>

/*
 * Arrange clients in a traditional tiling layout.
 *
 * Master windows (first nmaster clients) are placed in the left column,
 * each taking equal vertical space. Remaining windows go to the right
 * column (stack), also sharing vertical space equally.
 *
 * If there are fewer clients than nmaster, they fill the entire width.
 *
 * Parameters:
 *   m - The monitor to arrange windows on
 */
void
tile(Monitor *m)
{
	unsigned int mw, my, ty;
	int i, n = 0;
	Client *c;

	/* Count visible, non-floating, non-fullscreen clients */
	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen)
			n++;
	if (n == 0)
		return;

	/* Calculate master area width */
	if (n > m->nmaster)
		mw = m->nmaster ? (int)roundf(m->w.width * m->mfact) : 0;
	else
		mw = m->w.width;

	i = my = ty = 0;
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		if (i < m->nmaster) {
			int master_div = MIN(n, m->nmaster) - i;
			if (master_div <= 0)
				master_div = 1;
			/* Place in master area (left column) */
			resize(c, (struct wlr_box){.x = m->w.x, .y = m->w.y + my, .width = mw,
				.height = (m->w.height - my) / master_div}, 0);
			my += c->geom.height;
		} else {
			int stack_div = n - i;
			if (stack_div <= 0)
				stack_div = 1;
			/* Place in stack area (right column) */
			resize(c, (struct wlr_box){.x = m->w.x + mw, .y = m->w.y + ty,
				.width = m->w.width - mw, .height = (m->w.height - ty) / stack_div}, 0);
			ty += c->geom.height;
		}
		i++;
	}
}
