/* Overview: Super+O zooms the cursor monitor's camera out to frame every
 * window it holds (the same shot viewport_fit_all() already computes for
 * Super+0), and remembers where that camera was so it can snap back — on a
 * second Super+O, on clicking a window (buttonpress() in dwl.c), or on a bare
 * Escape (keypress() in modules/input/keyboard.c). Unlike niri's Overview
 * this isn't a separate renderer: kalin-wm's scene is already a real camera
 * over a real 2D canvas, so "zoom out" already is the overview — this module
 * just adds the toggle/remember-and-restore behavior around the existing
 * fit-all shot.
 *
 * Multi-camera: the overview is per-monitor state-wise too — it hijacks and
 * restores exactly one monitor's camera (the one under the cursor when it
 * opened), so a parked second monitor stays parked.
 *
 * Separately-compiled TU: links against dwl.c's externed globals (selmon,
 * viewport_fit_all, viewport_camera_tick, viewport_kick, status_mark_dirty)
 * via kalin.h. */
#include "kalin.h"

static int overview_active;
static Monitor *overview_mon; /* whose camera the overview hijacked */
static float saved_x, saved_y, saved_zoom;

int
overview_is_active(void)
{
	return overview_active;
}

void
overview_exit(void)
{
	Monitor *m = overview_mon;

	if (!overview_active)
		return;

	overview_active = 0;
	overview_mon = NULL;
	status_mark_dirty();

	if (!m)
		return;

	m->cam.target_x = saved_x;
	m->cam.target_y = saved_y;
	m->cam.target_zoom = saved_zoom;

	if (m->cam.smooth_pan) {
		m->cam.animating = 1;
		viewport_kick();
	} else {
		m->cam.x = saved_x;
		m->cam.y = saved_y;
		m->cam.zoom = saved_zoom;
		m->cam.animating = 0;
		viewport_camera_tick(m);
	}
}

void
toggle_overview(const Arg *arg)
{
	(void)arg;

	if (overview_active) {
		overview_exit();
		return;
	}

	if (!selmon)
		return;

	/* Save the camera's *destination*, not its current (possibly still
	 * mid-flight) position: cam.x/y/zoom are the animated, currently-
	 * interpolating values (see viewport_camera_tick()'s smooth-pan step),
	 * while target_x/y/zoom are where they're actually headed. Spamming
	 * Super+O faster than the pan animation settles used to sample .x/.y/
	 * .zoom at a different point along that interpolation each time, so
	 * each toggle-back restored to a slightly different "pre-overview"
	 * position than the last — a visible wiggle. target_x/y/zoom is stable
	 * regardless of how many times you toggle mid-animation: overview_exit()
	 * itself just sets target_x/y/zoom = saved_x/y/zoom and re-entering this
	 * function reads that same already-settled value right back. */
	overview_mon = selmon;
	saved_x = selmon->cam.target_x;
	saved_y = selmon->cam.target_y;
	saved_zoom = selmon->cam.target_zoom;
	overview_active = 1;

	viewport_fit_all(NULL);
}

/* Clicking a window in the overview jumps to it — centers the holder's camera
 * on it at 1.0 zoom, like it was navigated to normally, rather than restoring
 * the pre-Super+O view (that's what overview_exit()/Escape/toggle-close do).
 * Computes the centering offset directly for the *target* zoom (1.0), not
 * the still-zoomed-out live zoom — using viewport_center_on() here would
 * center for the wrong (about-to-change) zoom level, landing off target once
 * the animation finishes zooming in. */
void
overview_select(Client *c)
{
	Monitor *m;

	if (!overview_active)
		return;
	overview_active = 0;
	overview_mon = NULL;

	if (!c || !c->mon)
		return;
	m = c->mon;

	m->cam.target_zoom = 1.0f;
	m->cam.target_x = c->geom.x + c->geom.width / 2.0f - (float)m->w.width / 2.0f;
	m->cam.target_y = c->geom.y + c->geom.height / 2.0f - (float)m->w.height / 2.0f;

	if (m->cam.smooth_pan) {
		m->cam.animating = 1;
		viewport_kick();
	} else {
		m->cam.zoom = 1.0f;
		m->cam.x = m->cam.target_x;
		m->cam.y = m->cam.target_y;
		viewport_camera_tick(m);
	}

	status_mark_dirty();
}
