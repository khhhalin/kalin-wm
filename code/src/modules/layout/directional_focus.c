/* Directional focus navigation (cone search): Super+Arrow finds the nearest
 * window in roughly the pressed direction, independent of the connection-
 * graph (unlike swap_neighbor_dir(), connection_graph.c, which only acts on
 * an established neighbor link) — a pure geometric nearest-neighbor-in-a-cone
 * search over every currently visible client.
 *
 * Separately-compiled TU: links against dwl.c's externed globals (clients,
 * selmon, viewport, menu_shown) and functions (focustop, focusclient) via
 * kalin.h. Only focus_directional() is called from outside this file
 * (dwl.c's keybinding dispatch, forward-declared there since config.h needs
 * it); window_center()/angle_distance_in_cone()/cone_search_focus() are
 * internal to the cone-search algorithm. */
#include "kalin.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Get the center point of a window in world coordinates. */
static void
window_center(Client *c, float *cx, float *cy)
{
	if (!c || !cx || !cy)
		return;

	/* Validate geometry to prevent division issues */
	if (c->geom.width <= 0 || c->geom.height <= 0)
		return;

	*cx = c->geom.x + c->geom.width / 2.0f;
	*cy = c->geom.y + c->geom.height / 2.0f;
}

/* Check if a point is within a cone defined by center angle and width.
 * Returns the Euclidean distance if inside cone, -1.0f if outside. */
static float
angle_distance_in_cone(float dx, float dy, float center_angle, float cone_width)
{
	float angle = atan2f(dy, dx);
	float diff = fabsf(atan2f(sinf(angle - center_angle), cosf(angle - center_angle)));

	if (diff <= cone_width / 2.0f) {
		return sqrtf(dx * dx + dy * dy);
	}
	return -1.0f;
}

/* Core cone search algorithm - find nearest window in specified direction.
 * angle: direction in radians (0 = right, PI/2 = down, PI = left, -PI/2 = up)
 * cone_width: width of search cone in radians
 * Returns nearest client or NULL if none found. */
static Client *
cone_search_focus(float angle, float cone_width)
{
	Client *c, *nearest = NULL;
	Client *sel = focustop(selmon);
	float sel_cx, sel_cy;
	float min_dist = -1.0f;
	float c_cx, c_cy, dx, dy, dist;
	Monitor *m;
	float zoom;

	/* Get focus point - current window center or viewport center */
	if (sel) {
		window_center(sel, &sel_cx, &sel_cy);
	} else {
		/* No focused window - use viewport center in world coordinates */
		m = selmon;
		if (!m)
			return NULL;
		/* Viewport center in world coords - guard against zero zoom */
		zoom = viewport.zoom > 0.0f ? viewport.zoom : 1.0f;
		sel_cx = viewport.x + m->w.width / (2.0f * zoom);
		sel_cy = viewport.y + m->w.height / (2.0f * zoom);
	}

	/* Search for nearest window within cone */
	wl_list_for_each(c, &clients, link) {
		/* Skip if not visible, fullscreen, or is the current window */
		if (!VISIBLEON(c, selmon))
			continue;
		if (c->isfullscreen)
			continue;
		if (c == sel)
			continue;
		/* Panels (c->ispanel) aren't tileable windows to navigate between —
		 * they're shell chrome pinned to a fixed screen rect. */
		if (c->ispanel)
			continue;

		window_center(c, &c_cx, &c_cy);

		/* Calculate vector from focus to target */
		dx = c_cx - sel_cx;
		dy = c_cy - sel_cy;

		/* Check if within cone and get distance */
		dist = angle_distance_in_cone(dx, dy, angle, cone_width);
		if (dist >= 0.0f && (min_dist < 0.0f || dist < min_dist)) {
			min_dist = dist;
			nearest = c;
		}
	}

	return nearest;
}

/* Focus the nearest window in the specified direction.
 * Uses cone search with 90 degree initial cone, widening to 180 degrees if
 * no window found. */
void
focus_directional(const Arg *arg)
{
	Client *target = NULL;
	float angle;

	if (!selmon)
		return;

	/* See the matching guard in focusstack(): the hold-Super menu locks
	 * focus to the window it opened on. */
	if (menu_shown)
		return;

	/* Check if there are any clients to focus */
	if (wl_list_empty(&clients))
		return;

	/* Convert direction to angle in radians */
	switch (arg->i) {
	case DIR_LEFT:
		angle = (float)M_PI;  /* 180 degrees */
		break;
	case DIR_RIGHT:
		angle = 0.0f;  /* 0 degrees */
		break;
	case DIR_UP:
		angle = -(float)M_PI / 2.0f;  /* -90 degrees */
		break;
	case DIR_DOWN:
		angle = (float)M_PI / 2.0f;   /* 90 degrees */
		break;
	default:
		return;
	}

	/* First try with 90 degree cone */
	target = cone_search_focus(angle, (float)M_PI / 2.0f);

	/* If no window found, widen to 180 degrees */
	if (!target) {
		target = cone_search_focus(angle, (float)M_PI);
	}

	if (target) {
		wlr_log(WLR_DEBUG, "Focus directional: found window at (%d, %d)",
			target->geom.x, target->geom.y);
		focusclient(target, 1);
	}
}
