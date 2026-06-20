/*
 * Monocle layout for kalin-wm.
 *
 * This layout displays one window at a time, filling the entire screen.
 * All visible tiled windows are resized to fill the monitor area, stacked
 * on top of each other. The focused window is raised to the top.
 *
 * The layout symbol shows the count of windows in the monocle stack.
 * Useful for focusing on a single task while keeping other windows
 * available in the background.
 */

#include "../../include/layout.h"
#include <stdio.h>

/* External dependencies from dwl.c */
extern struct wl_list clients;
extern void resize(Client *c, struct wlr_box geo, int interact);
extern Client *focustop(Monitor *m);

/* Macro from dwl.c */
#define VISIBLEON(C, M) ((M) && (C)->mon == (M) && ((C)->tags & (M)->tagset[(M)->seltags]))
#define LENGTH(X) (sizeof X / sizeof X[0])

/*
 * Arrange clients in monocle (single-window) layout.
 *
 * All visible tiled windows are resized to fill the entire monitor
 * work area. They are stacked on top of each other, with the
 * focused window raised to the top.
 *
 * The layout symbol is updated to show the count of windows.
 *
 * Parameters:
 *   m - The monitor to arrange windows on
 */
void
monocle(Monitor *m)
{
	Client *c;
	int n = 0;

	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		/* Resize to fill entire monitor work area */
		resize(c, m->w, 0);
		n++;
	}
	
	/* Update layout symbol to show window count (e.g., "[3]") */
	if (n)
		snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "[%d]", n);
	
	/* Raise focused window to top of stack */
	if ((c = focustop(m)))
		wlr_scene_node_raise_to_top(&c->scene->node);
}
