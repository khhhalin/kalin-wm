/* Viewport camera operations: pan, zoom, follow, and animation tick.
 *
 * Separately-compiled TU: reads the shared `viewport` camera state and links
 * against dwl.c's externed globals/functions (selmon, mons, clients,
 * arrange, focustop, printstatus) via kalin.h. */
#include "kalin.h"

void viewport_tick(void); /* defined below; called from every monitor's frame callback */

/* Kick every enabled output into rendering one more frame. rendermon()
 * (dwl.c) already calls viewport_tick() unconditionally on every real output
 * frame, and re-requests another one (below, mirroring clients_anim_step())
 * for as long as viewport.animating stays set — so an animation just needs
 * *one* frame requested to start the self-sustaining chain, the same
 * mechanism window spring-glide already uses. No separate wall-clock timer:
 * once the camera settles, nothing keeps requesting frames and the output
 * goes back to fully idle. Schedules on every monitor rather than just
 * selmon since viewport_tick()'s dt-based easing doesn't care which output's
 * frame event drove it, and this way the animation can't stall if selmon
 * changes mid-flight. */
void
viewport_kick(void)
{
	Monitor *m;
	wl_list_for_each(m, &mons, link)
		if (m->wlr_output && m->wlr_output->enabled)
			wlr_output_schedule_frame(m->wlr_output);
}

static void
viewport_move_to(float x, float y, int smooth)
{
	viewport.target_x = x;
	viewport.target_y = y;

	if (smooth && viewport.smooth_pan) {
		viewport.animating = 1;
		viewport_kick();
	} else {
		viewport.x = x;
		viewport.y = y;
		viewport.animating = 0;
	}
}

/* Physics step size for the camera easing below — see the matching
 * ANIM_FIXED_DT in dwl.c's clients_anim_step() for why this needs a fixed
 * step rather than the raw per-call elapsed time: rendermon() can be invoked
 * far faster than any real display refresh on some backends, and stepping
 * with a vanishingly small dt makes near-zero real progress per call while
 * still doing full work and re-requesting a frame every time — a
 * self-sustaining busy loop. Banking real elapsed time into an accumulator
 * and only stepping once enough of it has built up keeps animation speed
 * identical to before while bounding how often the expensive part can run. */
#define VIEWPORT_FIXED_DT (1.0f / 120.0f)

/* Momentum ("flick") panning after a trackpad swipe lifts — see
 * viewport_coast_start() and gestures.c. Exponential velocity decay, same
 * fixed-step accumulator viewport_tick() already uses for the target-lerp
 * case, just a different physics model while viewport.coasting is set. */
#define COAST_FRICTION_PER_SEC 3.5f   /* higher = friction wins sooner */
#define COAST_MIN_START_SPEED  120.0f /* world units/sec; below this, don't bother coasting */
#define COAST_STOP_SPEED       15.0f  /* world units/sec; below this, coast is considered settled */

void
viewport_tick(void)
{
	static struct timespec last;
	static int have_last = 0;
	static float accum = 0.0f;
	struct timespec now;
	float elapsed, k;
	float dx, dy, dz;

	/* Defensive: keep zoom finite and in a sane range on EVERY frame (before the
	 * not-animating early-out), so a NaN/inf or near-zero zoom can never reach
	 * WORLD_TO_SCREEN — enormous coordinates hang a real GPU driver and freeze
	 * the whole session. Cheap insurance. */
	if (isnan(viewport.target_zoom) || viewport.target_zoom < 0.05f)
		viewport.target_zoom = 1.0f;
	else if (viewport.target_zoom > 20.0f)
		viewport.target_zoom = 20.0f;
	if (isnan(viewport.zoom) || viewport.zoom < 0.05f)
		viewport.zoom = 1.0f;
	else if (viewport.zoom > 20.0f)
		viewport.zoom = 20.0f;

	if (!viewport.animating || !selmon) {
		have_last = 0;
		accum = 0.0f;
		return;
	}

	/* Frame-rate-independent easing: measure real elapsed time so the camera
	 * converges at the same rate regardless of refresh rate. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (!have_last) {
		elapsed = VIEWPORT_FIXED_DT;
		have_last = 1;
		accum = 0.0f;
	} else {
		elapsed = (float)(now.tv_sec - last.tv_sec)
			+ (float)(now.tv_nsec - last.tv_nsec) / 1e9f;
	}
	last = now;
	if (elapsed <= 0.0f)
		elapsed = VIEWPORT_FIXED_DT;
	if (elapsed > 0.1f)
		elapsed = 0.1f; /* clamp after a stall so we don't jump */
	accum += elapsed;

	while (accum >= VIEWPORT_FIXED_DT && viewport.animating) {
		accum -= VIEWPORT_FIXED_DT;

		if (viewport.coasting) {
			float speed, friction;

			viewport.x += viewport.vel_x * VIEWPORT_FIXED_DT;
			viewport.y += viewport.vel_y * VIEWPORT_FIXED_DT;
			viewport.target_x = viewport.x;
			viewport.target_y = viewport.y;

			friction = expf(-COAST_FRICTION_PER_SEC * VIEWPORT_FIXED_DT);
			viewport.vel_x *= friction;
			viewport.vel_y *= friction;
			speed = hypotf(viewport.vel_x, viewport.vel_y);

			viewport_camera_tick(selmon);

			if (speed < COAST_STOP_SPEED) {
				viewport.coasting = 0;
				viewport.animating = 0;
				viewport.vel_x = 0.0f;
				viewport.vel_y = 0.0f;
				have_last = 0;
				accum = 0.0f;
				status_mark_dirty();
			}
			continue;
		}

		/* Critically-damped exponential approach toward the target. */
		k = 1.0f - expf(-18.0f * VIEWPORT_FIXED_DT);

		dx = viewport.target_x - viewport.x;
		dy = viewport.target_y - viewport.y;
		dz = viewport.target_zoom - viewport.zoom;

		if (fabsf(dx) < 0.5f && fabsf(dy) < 0.5f && fabsf(dz) < 0.001f) {
			viewport.x = viewport.target_x;
			viewport.y = viewport.target_y;
			viewport.zoom = viewport.target_zoom;
			viewport.animating = 0;
			have_last = 0;
			accum = 0.0f;
			/* Not arrange(): a camera move alone never changes window
			 * positions; the final exact snap only needs the
			 * camera-dependent refresh. */
			viewport_camera_tick(selmon);
			/* Camera settled: ask clients to re-render at the final zoom DPI so
			 * zoomed content is crisp rather than upscaled. */
			client_apply_zoom_scale();
			/* Publish the settled state to status/IPC/foreign-toplevel so the
			 * shell OSD shows the final zoom level. */
			status_mark_dirty();
			break;
		}

		viewport.x += dx * k;
		viewport.y += dy * k;
		viewport.zoom += dz * k;
		/* Not the full arrange(): the visibility/state bookkeeping inside
		 * arrange() is independent of the camera, so re-running it on every
		 * step is wasted work; viewport_camera_tick()
		 * covers what actually depends on viewport.x/y/zoom (wallpaper,
		 * on-screen window position, pointer hit-test). */
		viewport_camera_tick(selmon);
	}
}

/* Start a momentum coast at the given world-units/sec velocity — called
 * from gestures.c when a 3-finger swipe's fingers lift with enough speed to
 * feel like a deliberate flick, rather than fully committing every swipe
 * release to a coast (a slow, deliberate-stop swipe shouldn't drift on).
 * No-ops below COAST_MIN_START_SPEED. */
void
viewport_coast_start(float vx, float vy)
{
	if (hypotf(vx, vy) < COAST_MIN_START_SPEED)
		return;
	viewport.vel_x = vx;
	viewport.vel_y = vy;
	viewport.coasting = 1;
	viewport.animating = 1;
	viewport_kick();
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

	/* Camera-only: never needs a layout recompute, just the camera-dependent
	 * refresh (wallpaper, on-screen window position, pointer hit-test). */
	if (selmon && !viewport.animating)
		viewport_camera_tick(selmon);

	status_mark_dirty();
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
		viewport_kick();
	} else {
		viewport.zoom = tz;
		if (selmon)
			viewport_camera_tick(selmon);
	}

	status_mark_dirty();
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
		viewport_kick();
	} else {
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.zoom = 1.0f;
		viewport.animating = 0;
		if (selmon)
			viewport_camera_tick(selmon);
	}

	status_mark_dirty();
}

/* Frame all windows on the focused monitor: zoom/pan so the whole canvas
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
		/* Panels (c->ispanel) sit in screen space at a fixed rect, not
		 * world space — folding one into this bounding box (Super+0, and
		 * Super+O's overview via toggle_overview()) skewed the fit toward
		 * wherever that rect happens to sit on screen instead of framing
		 * the real windows. */
		if (!VISIBLEON(c, m) || c->isfullscreen || c->ispanel)
			continue;
		if (!found) {
			minx = c->geom.x;
			miny = c->geom.y;
			maxx = c->geom.x + c->geom.width;
			maxy = c->geom.y + c->geom.height;
			found = 1;
		} else {
			if (c->geom.x < minx) minx = c->geom.x;
			if (c->geom.y < miny) miny = c->geom.y;
			if (c->geom.x + c->geom.width > maxx) maxx = c->geom.x + c->geom.width;
			if (c->geom.y + c->geom.height > maxy) maxy = c->geom.y + c->geom.height;
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
		viewport_kick();
	} else {
		viewport.zoom = z;
		viewport.x = viewport.target_x;
		viewport.y = viewport.target_y;
		viewport_camera_tick(m);
	}

	status_mark_dirty();
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
		c->geom.x + c->geom.width / 2.0f - m->w.width / (2.0f * z),
		c->geom.y + c->geom.height / 2.0f - m->w.height / (2.0f * z),
		1
	);

	if (!viewport.animating)
		viewport_camera_tick(m);
}

/* Pan the camera the minimum amount needed so c's whole geometry is within
 * the visible viewport, instead of re-centering on it. A no-op if c is
 * already fully visible. On an axis where c is bigger than the viewport,
 * aligns to c's near (top/left) edge rather than splitting the difference,
 * matching ordinary "scroll into view" behavior.
 *
 * Used for passive follow (viewport_follow_focus(), below) so routine focus
 * changes (Super+Arrow, Super+J/K, click) don't yank the camera to dead
 * center every time — only far enough to keep the window on screen. Deciding
 * "explicit jump" (viewport_center_on, still used for e.g. the taskbar's
 * fly-to-window and auto-pan to a newly spawned window) vs. "passive follow"
 * (this) is a per-call-site choice, not a global setting. */
static void
viewport_ensure_visible(Client *c)
{
	Monitor *m;
	float z, vw, vh;
	float wx0, wy0, wx1, wy1;
	float nx, ny;

	if (!c || !c->mon)
		return;

	m = c->mon;
	z = viewport.zoom > 0.0f ? viewport.zoom : 1.0f;
	vw = m->w.width / z;
	vh = m->w.height / z;

	wx0 = c->geom.x;
	wy0 = c->geom.y;
	wx1 = c->geom.x + c->geom.width;
	wy1 = c->geom.y + c->geom.height;

	nx = viewport.x;
	ny = viewport.y;

	if (wx1 - wx0 > vw || wx0 < viewport.x)
		nx = wx0;
	else if (wx1 > viewport.x + vw)
		nx = wx1 - vw;

	if (wy1 - wy0 > vh || wy0 < viewport.y)
		ny = wy0;
	else if (wy1 > viewport.y + vh)
		ny = wy1 - vh;

	if (nx == viewport.x && ny == viewport.y)
		return; /* already fully visible: no camera movement at all */

	viewport_move_to(nx, ny, 1);
	if (!viewport.animating)
		viewport_camera_tick(m);
}

/* When the hold-Super menu (see WindowActions.qml) opens on a window that
 * isn't (near) full monitor width, its arc flies out from the window's
 * right screen edge and can run past the right edge of the screen if the
 * window itself already sits close to it. Pan the camera right by just
 * enough screen-space to make room. Mirrors the shell's own dock-mode
 * threshold (85% of monitor width) so this never fires for an already-full-
 * width window, which docks the menu to the screen edge regardless of the
 * window's own position — see menu_shown's caller in dwl.c. */
#define MENU_ARC_RESERVE_PX 300 /* approx. arc footprint past the window's right edge */
void
viewport_menu_reveal(Client *c)
{
	Monitor *m;
	float z, win_w_screen, win_right_screen, overflow;
	float d[2];
	Arg a;

	if (!c || !c->mon)
		return;
	m = c->mon;
	z = viewport.zoom > 0.0f ? viewport.zoom : 1.0f;

	win_w_screen = c->geom.width * z;
	if (win_w_screen >= m->w.width * 0.85f)
		return; /* shell already docks the menu to the screen edge */

	win_right_screen = (c->geom.x - viewport.x) * z + win_w_screen;
	overflow = win_right_screen + MENU_ARC_RESERVE_PX - m->w.width;
	if (overflow <= 0.0f)
		return; /* already enough room to the window's right */

	d[0] = overflow;
	d[1] = 0.0f;
	a.v = d;
	viewport_pan(&a);
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
		if (c)
			viewport_center_on(c);
	}
	
	status_mark_dirty();
}

/* Toggle auto-pan to new windows */
void
viewport_toggle_follow_new(const Arg *arg)
{
	(void)arg;
	
	viewport.follow_new_windows = !viewport.follow_new_windows;
	wlr_log(WLR_INFO, "Auto-pan to new windows: %s", 
		viewport.follow_new_windows ? "enabled" : "disabled");
	
	status_mark_dirty();
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

	cx = c->geom.x + c->geom.width / 2.0f;
	cy = c->geom.y + c->geom.height / 2.0f;
	viewport.target_zoom = z;
	/* Bias the window left of centre (~40% of the width) so the Android-style
	 * side menu has room to fan out on its right. */
	viewport.target_x = cx - (float)m->w.width * 0.40f / z;
	viewport.target_y = cy - (float)m->w.height / (2.0f * z);
	viewport.animating = 1;
	viewport_kick();
	status_mark_dirty();
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
	viewport_kick();
	status_mark_dirty();
}

/* Update camera position when following a window - call this on focus change */
void
viewport_follow_focus(void)
{
	Client *c;
	
	if (!viewport.follow || !selmon)
		return;
	
	c = focustop(selmon);
	/* Panels (c->ispanel) live in screen space, not world space — their
	 * c->geom is a screen-pixel rect (bottom-right-ish for these panels),
	 * not a world position. Treating it as one here made focusing a docked
	 * panel (e.g. clicking into it to type) send the camera flying toward
	 * wherever that rect happens to sit on screen, every time. */
	if (c && !c->ispanel) {
		/* Pan just enough to keep it on screen, not center on it — see
		 * viewport_ensure_visible()'s comment for why. */
		viewport_ensure_visible(c);
	}
}

/* --- Camera drag-pan (Super+Ctrl+LMB on empty canvas) ---
 *
 * Direct manipulation: the world point under the cursor at grab-start stays
 * under the cursor for the duration of the drag. Grab-start state is stashed
 * in these file-statics rather than the shared `viewport` struct since it's
 * only meaningful while cursor_mode == CurPan.
 *
 * The grab-start itself (arming cursor_mode == CurPan) lives in dwl.c's
 * bind_invoke(), not here: it needs xytonode()/cursor_mode, both internal-
 * linkage statics owned by dwl.c that aren't exported to other TUs. This
 * function only stashes the anchor point once dwl.c has decided to start
 * the pan. */
static double pan_screen_x0, pan_screen_y0;
static float pan_view_x0, pan_view_y0;

void
viewport_pan_grab_start(void)
{
	pan_screen_x0 = cursor->x;
	pan_screen_y0 = cursor->y;
	pan_view_x0 = viewport.x;
	pan_view_y0 = viewport.y;
}

/* Per-motion-event update while CurPan is active; called from
 * motionnotify()'s CurPan branch. */
void
viewport_pan_grab_update(void)
{
	float z = viewport.zoom > 0.0f ? viewport.zoom : 1.0f;

	viewport.x = pan_view_x0 - (float)(cursor->x - pan_screen_x0) / z;
	viewport.y = pan_view_y0 - (float)(cursor->y - pan_screen_y0) / z;
	viewport.target_x = viewport.x;
	viewport.target_y = viewport.y;
	viewport.animating = 0;

	if (selmon)
		viewport_camera_tick(selmon);
	status_mark_dirty();
}
