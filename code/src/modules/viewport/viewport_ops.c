/* Viewport camera operations: pan, zoom, follow, and animation tick.
 *
 * Separately-compiled TU: reads the shared `viewport` camera state and links
 * against dwl.c's externed globals/functions (selmon, clients, event_loop,
 * arrange, focustop, printstatus) via kalin.h. */
#include "kalin.h"

void viewport_tick(void); /* defined below; used by the animation timer */

/* Drive animations from an event-loop timer (~60 Hz) rather than relying on
 * output frame callbacks, which don't free-run when the output is idle (or on
 * the headless backend). The timer re-arms itself while animating and stops
 * once the camera settles. */
static struct wl_event_source *viewport_anim_timer = NULL;

static int
viewport_anim_timer_cb(void *data)
{
	(void)data;
	viewport_tick();
	if (viewport.animating && viewport_anim_timer)
		wl_event_source_timer_update(viewport_anim_timer, 16);
	return 0;
}

static void
viewport_schedule_frame(void)
{
	if (!event_loop)
		return;
	if (!viewport_anim_timer)
		viewport_anim_timer = wl_event_loop_add_timer(event_loop,
				viewport_anim_timer_cb, NULL);
	if (viewport_anim_timer)
		wl_event_source_timer_update(viewport_anim_timer, 16);
}

static void
viewport_move_to(float x, float y, int smooth)
{
	viewport.target_x = x;
	viewport.target_y = y;

	if (smooth && viewport.smooth_pan) {
		viewport.animating = 1;
		viewport_schedule_frame();
	} else {
		viewport.x = x;
		viewport.y = y;
		viewport.animating = 0;
	}
}

void
viewport_tick(void)
{
	static struct timespec last;
	static int have_last = 0;
	struct timespec now;
	float dt, k;
	float dx, dy, dz;

	if (!viewport.animating || !selmon) {
		have_last = 0;
		return;
	}

	/* Frame-rate-independent easing: measure real elapsed time so the camera
	 * converges at the same rate regardless of refresh rate. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (!have_last) {
		dt = 1.0f / 60.0f;
		have_last = 1;
	} else {
		dt = (float)(now.tv_sec - last.tv_sec)
			+ (float)(now.tv_nsec - last.tv_nsec) / 1e9f;
	}
	last = now;
	if (dt <= 0.0f)
		dt = 1.0f / 60.0f;
	if (dt > 0.1f)
		dt = 0.1f; /* clamp after a stall so we don't jump */

	/* Critically-damped exponential approach toward the target. */
	k = 1.0f - expf(-18.0f * dt);

	dx = viewport.target_x - viewport.x;
	dy = viewport.target_y - viewport.y;
	dz = viewport.target_zoom - viewport.zoom;

	if (fabsf(dx) < 0.5f && fabsf(dy) < 0.5f && fabsf(dz) < 0.001f) {
		viewport.x = viewport.target_x;
		viewport.y = viewport.target_y;
		viewport.zoom = viewport.target_zoom;
		viewport.animating = 0;
		have_last = 0;
		arrange(selmon);
		/* Camera settled: ask clients to re-render at the final zoom DPI so
		 * zoomed content is crisp rather than upscaled. */
		client_apply_zoom_scale();
		/* Publish the settled state to status/IPC/foreign-toplevel so the
		 * shell OSD shows the final zoom level. */
		printstatus();
		return;
	}

	viewport.x += dx * k;
	viewport.y += dy * k;
	viewport.zoom += dz * k;
	arrange(selmon);
}

void
viewport_pan(const Arg *arg)
{
	float *d = (float *)arg->v;
	float z;
	float tx, ty;
	
	/* Pan speed is inverse to zoom (faster when zoomed out) */
	z = viewport.zoom > 0.0f ? viewport.zoom : 1.0f;
	tx = viewport.target_x + d[0] / z;
	ty = viewport.target_y + d[1] / z;
	viewport_move_to(tx, ty, 1);

	if (selmon && !viewport.animating)
		arrange(selmon);
	
	printstatus();
}

void
viewport_zoom(const Arg *arg)
{
	float factor = arg->f;
	float tz;
	if (factor <= 0.0f)
		return;

	/* Multiply the *target* so repeated presses accumulate smoothly. */
	tz = viewport.target_zoom * factor;
	if (tz < 0.1f)
		tz = 0.1f;
	if (tz > 5.0f)
		tz = 5.0f;
	viewport.target_zoom = tz;

	wlr_log(WLR_DEBUG, "Viewport zoom target: %.2f", viewport.target_zoom);

	if (viewport.smooth_pan) {
		viewport.animating = 1;
		viewport_schedule_frame();
	} else {
		viewport.zoom = tz;
		if (selmon)
			arrange(selmon);
	}

	printstatus();
}

void
viewport_reset(const Arg *arg)
{
	(void)arg; /* unused */

	viewport.target_x = 0.0f;
	viewport.target_y = 0.0f;
	viewport.target_zoom = 1.0f;

	if (viewport.smooth_pan) {
		viewport.animating = 1;
		viewport_schedule_frame();
	} else {
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.zoom = 1.0f;
		viewport.animating = 0;
		if (selmon)
			arrange(selmon);
	}

	printstatus();
}

/* Frame all tiled windows on the focused monitor: zoom/pan so the whole canvas
 * fits on screen. The primary "where is everything / get me back" navigation. */
void
viewport_fit_all(const Arg *arg)
{
	Client *c;
	Monitor *m = selmon;
	float minx = 0, miny = 0, maxx = 0, maxy = 0;
	float bw_, bh_, cx, cy, zx, zy, z;
	int found = 0;
	(void)arg;

	if (!m || m->w.width <= 0 || m->w.height <= 0)
		return;

	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen || !c->world.set)
			continue;
		if (!found) {
			minx = c->world.x;
			miny = c->world.y;
			maxx = c->world.x + c->geom.width;
			maxy = c->world.y + c->geom.height;
			found = 1;
		} else {
			if (c->world.x < minx) minx = c->world.x;
			if (c->world.y < miny) miny = c->world.y;
			if (c->world.x + c->geom.width > maxx) maxx = c->world.x + c->geom.width;
			if (c->world.y + c->geom.height > maxy) maxy = c->world.y + c->geom.height;
		}
	}

	if (!found) {
		viewport_reset(NULL); /* nothing to fit: go home */
		return;
	}

	bw_ = maxx - minx;
	bh_ = maxy - miny;
	if (bw_ < 1.0f) bw_ = 1.0f;
	if (bh_ < 1.0f) bh_ = 1.0f;

	/* Fit the bounding box with a 10% margin; never zoom in past 1.0. */
	zx = (float)m->w.width / bw_;
	zy = (float)m->w.height / bh_;
	z = (zx < zy ? zx : zy) * 0.9f;
	if (z < 0.1f) z = 0.1f;
	if (z > 1.0f) z = 1.0f;

	cx = (minx + maxx) / 2.0f;
	cy = (miny + maxy) / 2.0f;

	viewport.target_zoom = z;
	viewport.target_x = cx - (float)m->w.width / (2.0f * z);
	viewport.target_y = cy - (float)m->w.height / (2.0f * z);

	if (viewport.smooth_pan) {
		viewport.animating = 1;
		viewport_schedule_frame();
	} else {
		viewport.zoom = z;
		viewport.x = viewport.target_x;
		viewport.y = viewport.target_y;
		arrange(m);
	}

	printstatus();
}

/* Center camera on a specific window */
void
viewport_center_on(Client *c)
{
	Monitor *m;
	float z;
	if (!c || !c->mon)
		return;
	
	m = c->mon;
	z = viewport.zoom > 0.0f ? viewport.zoom : 1.0f;
	
	/* Center the camera so the window appears in the middle of the monitor */
	viewport_move_to(
		c->world.x + c->geom.width / 2.0f - m->w.width / (2.0f * z),
		c->world.y + c->geom.height / 2.0f - m->w.height / (2.0f * z),
		1
	);

	if (!viewport.animating)
		arrange(m);
}

/* Toggle camera follow mode */
void
viewport_toggle_follow(const Arg *arg)
{
	(void)arg;
	
	viewport.follow = !viewport.follow;
	
	wlr_log(WLR_INFO, "Camera follow mode: %s", 
		viewport.follow ? "enabled" : "disabled");
	
	if (viewport.follow && selmon) {
		Client *c = focustop(selmon);
		if (c && c->world.set)
			viewport_center_on(c);
	}
	
	printstatus();
}

/* Toggle auto-pan to new windows */
void
viewport_toggle_follow_new(const Arg *arg)
{
	(void)arg;
	
	viewport.follow_new_windows = !viewport.follow_new_windows;
	wlr_log(WLR_INFO, "Auto-pan to new windows: %s", 
		viewport.follow_new_windows ? "enabled" : "disabled");
	
	printstatus();
}

/* Fit + center the camera on a single window (zoom in to fill, with margin).
 * Used by the hold-Super spotlight to focus the active window. */
void
viewport_focus_window(Client *c)
{
	Monitor *m;
	float bw_, bh_, zx, zy, z, cx, cy;

	if (!c || !c->mon)
		return;
	m = c->mon;
	if (m->w.width <= 0 || m->w.height <= 0)
		return;

	bw_ = c->geom.width  < 1.0f ? 1.0f : (float)c->geom.width;
	bh_ = c->geom.height < 1.0f ? 1.0f : (float)c->geom.height;
	zx = (float)m->w.width / bw_;
	zy = (float)m->w.height / bh_;
	z = (zx < zy ? zx : zy) * 0.55f;  /* margin: window ~55% of the viewport */
	if (z < 0.1f) z = 0.1f;
	if (z > 2.0f) z = 2.0f;           /* spotlight may zoom IN, unlike fit-all */

	cx = c->world.x + c->geom.width / 2.0f;
	cy = c->world.y + c->geom.height / 2.0f;
	viewport.target_zoom = z;
	/* Bias the window left of centre (~40% of the width) so the Android-style
	 * side menu has room to fan out on its right. */
	viewport.target_x = cx - (float)m->w.width * 0.40f / z;
	viewport.target_y = cy - (float)m->w.height / (2.0f * z);
	viewport.animating = 1;
	viewport_schedule_frame();
	printstatus();
}

/* Animate the camera back to an explicit (x, y, zoom) — restores the
 * pre-spotlight view. */
void
viewport_animate_to(float x, float y, float zoom)
{
	viewport.target_x = x;
	viewport.target_y = y;
	viewport.target_zoom = zoom;
	viewport.animating = 1;
	viewport_schedule_frame();
	printstatus();
}

/* Update camera position when following a window - call this on focus change */
void
viewport_follow_focus(void)
{
	Client *c;
	
	if (!viewport.follow || !selmon)
		return;
	
	c = focustop(selmon);
	if (c && c->world.set) {
		/* Smoothly center on the focused window */
		viewport_center_on(c);
	}
}
