/* Overview: Super+O zooms the camera out to frame every window (the same shot
 * viewport_fit_all() already computes for Super+0), and remembers where the
 * camera was so it can snap back — on a second Super+O, on clicking a window
 * (buttonpress() in dwl.c), or on a bare Escape (keypress() in
 * modules/input/keyboard.c). Unlike niri's Overview this isn't a separate
 * renderer: kalin-wm's scene is already a real camera over a real 2D canvas,
 * so "zoom out" already is the overview — this module just adds the
 * toggle/remember-and-restore behavior around the existing fit-all shot.
 *
 * Separately-compiled TU: reads/writes the shared `viewport` camera state and
 * links against dwl.c's externed globals (selmon, viewport_fit_all,
 * viewport_camera_tick, viewport_kick, status_mark_dirty) via kalin.h. */
#include "kalin.h"

static int overview_active;
static float saved_x, saved_y, saved_zoom;

int
overview_is_active(void)
{
	return overview_active;
}

void
overview_exit(void)
{
	if (!overview_active)
		return;

	viewport.target_x = saved_x;
	viewport.target_y = saved_y;
	viewport.target_zoom = saved_zoom;

	if (viewport.smooth_pan) {
		viewport.animating = 1;
		viewport_kick();
	} else {
		viewport.x = saved_x;
		viewport.y = saved_y;
		viewport.zoom = saved_zoom;
		viewport.animating = 0;
		if (selmon)
			viewport_camera_tick(selmon);
	}

	overview_active = 0;
	status_mark_dirty();
}

void
toggle_overview(const Arg *arg)
{
	(void)arg;

	if (overview_active) {
		overview_exit();
		return;
	}

	/* Save the camera's *destination*, not its current (possibly still
	 * mid-flight) position: viewport.x/y/zoom are the animated, currently-
	 * interpolating values (see viewport_camera_tick()'s smooth-pan step),
	 * while target_x/y/zoom are where they're actually headed. Spamming
	 * Super+O faster than the pan animation settles used to sample .x/.y/
	 * .zoom at a different point along that interpolation each time, so
	 * each toggle-back restored to a slightly different "pre-overview"
	 * position than the last — a visible wiggle. target_x/y/zoom is stable
	 * regardless of how many times you toggle mid-animation: overview_exit()
	 * itself just sets target_x/y/zoom = saved_x/y/zoom and re-enters this
	 * function reads that same already-settled value right back. */
	saved_x = viewport.target_x;
	saved_y = viewport.target_y;
	saved_zoom = viewport.target_zoom;
	overview_active = 1;

	viewport_fit_all(NULL);
}

/* Clicking a window in the overview jumps to it — centers the camera on it
 * at 1.0 zoom, like it was navigated to normally, rather than restoring the
 * pre-Super+O view (that's what overview_exit()/Escape/toggle-close do).
 * Computes the centering offset directly for the *target* zoom (1.0), not
 * the still-zoomed-out live viewport.zoom — using viewport_center_on() here
 * would center for the wrong (about-to-change) zoom level, landing off
 * target once the animation finishes zooming in. */
void
overview_select(Client *c)
{
	Monitor *m;

	if (!overview_active)
		return;
	overview_active = 0;

	if (!c || !c->mon)
		return;
	m = c->mon;

	viewport.target_zoom = 1.0f;
	viewport.target_x = c->geom.x + c->geom.width / 2.0f - (float)m->w.width / 2.0f;
	viewport.target_y = c->geom.y + c->geom.height / 2.0f - (float)m->w.height / 2.0f;

	if (viewport.smooth_pan) {
		viewport.animating = 1;
		viewport_kick();
	} else {
		viewport.zoom = 1.0f;
		viewport.x = viewport.target_x;
		viewport.y = viewport.target_y;
		viewport_camera_tick(m);
	}

	status_mark_dirty();
}
