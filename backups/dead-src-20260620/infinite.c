/*
 * Infinite scroll layout for kalin-wm (inspired by Niri).
 *
 * This layout places windows in a scrollable, infinite canvas.
 * Windows keep their natural size and are positioned in "world coordinates"
 * that persist across layout changes. New windows are placed to the right
 * of existing windows.
 *
 * The viewport (camera position) can be panned and zoomed to navigate
 * the infinite workspace. Windows maintain their world positions, making
 * this ideal for workflows that benefit from spatial memory.
 *
 * Key features:
 * - Windows keep natural size (not forced to fill screen)
 * - Persistent world coordinates
 * - Horizontal arrangement with configurable gaps
 * - Viewport panning and zooming support
 */

#include "../../include/layout.h"

/* External dependencies from dwl.c */
extern struct wl_list clients;
extern void resize(Client *c, struct wlr_box geo, int interact);

/* Helper functions - duplicated from dwl.c */
static float
infinite_rightmost_edge(Monitor *m)
{
	Client *c;
	float max_edge = 0;
	int n = 0;
	
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen && c->world.set) {
			float edge = c->world.x + c->geom.width;
			if (edge > max_edge)
				max_edge = edge;
			n++;
		}
	}
	return n > 0 ? max_edge : 0;
}

static float
infinite_topmost_y(Monitor *m)
{
	Client *c;
	float min_y = 0;
	int n = 0;
	
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen && c->world.set) {
			if (!n || c->world.y < min_y)
				min_y = c->world.y;
			n++;
		}
	}
	return n > 0 ? min_y : 0;
}

static void
infinite_place_window(Client *c, Monitor *m)
{
	float x, y;
	
	if (c->world.set)
		return;
	
	x = infinite_rightmost_edge(m);
	y = infinite_topmost_y(m);
	
	c->world.x = x;
	c->world.y = y;
	c->world.set = 1;
}

/* Gap between windows in pixels */
#define INFINITE_GAP 10

/*
 * Arrange clients in an infinite scroll layout.
 *
 * Windows are positioned according to their world coordinates.
 * New windows without world coordinates are assigned a position
 * to the right of existing windows. Windows maintain their natural
 * size and are separated by a configurable gap.
 *
 * This layout is ideal for workflows requiring spatial organization
 * and persistent window positioning.
 *
 * Parameters:
 *   m - The monitor to arrange windows on
 */
void
infinite(Monitor *m)
{
	Client *c;
	struct wlr_box geo;
	unsigned int gap = INFINITE_GAP;
	static int in_infinite_arrange = 0;

	if (!m || m->w.width <= 0 || m->w.height <= 0)
		return;

	if (in_infinite_arrange)
		return;
	in_infinite_arrange = 1;
	
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		
		/* Assign world position if new window */
		if (!c->world.set) {
			infinite_place_window(c, m);
			/* For new windows, set a reasonable default size */
			c->geom.width = (int)(m->w.width * 0.6f);
			c->geom.height = (int)(m->w.height * 0.7f);
		}
		
		/* Position in world coordinates - keep natural window size */
		geo.x = (int)c->world.x + gap;
		geo.y = (int)c->world.y + gap;
		geo.width = c->geom.width;
		geo.height = c->geom.height;
		
		resize(c, geo, 0);
	}

	in_infinite_arrange = 0;
}
