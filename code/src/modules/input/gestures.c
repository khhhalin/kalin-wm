/* Trackpad gesture navigation: a 3-finger swipe pans the camera (gaining
 * momentum once fingers lift, if the swipe was fast enough), a pinch zooms
 * it. Inspired by driftwm's gesture-driven canvas navigation (see the
 * ledger) — kalin-wm previously had zero touchpad gesture support at all.
 *
 * Uses wlroots' native swipe/pinch events directly off wlr_pointer — the
 * same libinput gesture recognition a touchpad's pointer device already
 * does, exposed as plain wl_signals (wlr_pointer.events.swipe_begin and
 * friends). No new Wayland protocol dependency; nothing here is visible to
 * clients.
 *
 * Separately-compiled TU: links against dwl.c's externed viewport global
 * and viewport_camera_tick/status_mark_dirty/selmon via kalin.h. */
#include "kalin.h"

#define GESTURE_PAN_FINGERS 3 /* fewer (1-2) already mean click-drag/scroll */

/* Per-pointer-device gesture state. One touchpad is the only realistic case
 * on a personal laptop, but this is still allocated per-device (not a file-
 * scope global) and cleaned up on device destroy, rather than assuming
 * exactly one pointer ever exists. */
struct GestureDevice {
	struct wl_listener swipe_begin;
	struct wl_listener swipe_update;
	struct wl_listener swipe_end;
	struct wl_listener pinch_begin;
	struct wl_listener pinch_update;
	struct wl_listener destroy;

	int swipe_active;
	uint32_t swipe_last_msec;
	float vel_x, vel_y; /* smoothed world-units/sec estimate, live during a swipe */

	int pinch_active;
	float pinch_begin_zoom;
};

static void
gesture_swipe_begin(struct wl_listener *listener, void *data)
{
	struct GestureDevice *g = wl_container_of(listener, g, swipe_begin);
	struct wlr_pointer_swipe_begin_event *event = data;

	if (event->fingers != GESTURE_PAN_FINGERS)
		return;
	g->swipe_active = 1;
	g->swipe_last_msec = event->time_msec;
	g->vel_x = 0.0f;
	g->vel_y = 0.0f;
	/* A live gesture is direct manipulation: stop any in-flight easing or
	 * coast so the fingers and the camera don't fight each other. Gestures
	 * act on the monitor under the cursor (multi-camera). */
	if (selmon) {
		selmon->cam.animating = 0;
		selmon->cam.coasting = 0;
	}
}

static void
gesture_swipe_update(struct wl_listener *listener, void *data)
{
	struct GestureDevice *g = wl_container_of(listener, g, swipe_update);
	struct wlr_pointer_swipe_update_event *event = data;
	float z, dt, wx, wy, ivx, ivy;

	if (!g->swipe_active || !selmon)
		return;

	z = MON_ZOOM_SAFE(selmon);
	dt = (float)(event->time_msec - g->swipe_last_msec) / 1000.0f;
	g->swipe_last_msec = event->time_msec;
	if (dt <= 0.0f)
		dt = 1.0f / 120.0f;

	/* Content follows the fingers — same convention as the existing
	 * Super+Ctrl+LMB direct-manipulation pan-grab (viewport_pan_grab_update). */
	wx = (float)event->dx / z;
	wy = (float)event->dy / z;
	selmon->cam.x -= wx;
	selmon->cam.y -= wy;
	selmon->cam.target_x = selmon->cam.x;
	selmon->cam.target_y = selmon->cam.y;

	/* Exponential moving average of instantaneous velocity, so a brief
	 * pause right before lifting fingers doesn't leave a stale high
	 * velocity that then launches an unwanted coast. */
	ivx = -wx / dt;
	ivy = -wy / dt;
	g->vel_x = g->vel_x * 0.7f + ivx * 0.3f;
	g->vel_y = g->vel_y * 0.7f + ivy * 0.3f;

	viewport_camera_tick(selmon);
	status_mark_dirty();
}

static void
gesture_swipe_end(struct wl_listener *listener, void *data)
{
	struct GestureDevice *g = wl_container_of(listener, g, swipe_end);
	struct wlr_pointer_swipe_end_event *event = data;

	if (!g->swipe_active)
		return;
	g->swipe_active = 0;
	/* viewport_coast_start() itself no-ops below its own min-speed
	 * threshold, so a slow, deliberate-stop swipe just stays put. */
	if (!event->cancelled)
		viewport_coast_start(selmon, g->vel_x, g->vel_y);
}

static void
gesture_pinch_begin(struct wl_listener *listener, void *data)
{
	struct GestureDevice *g = wl_container_of(listener, g, pinch_begin);
	(void)data;
	g->pinch_active = 1;
	g->pinch_begin_zoom = selmon ? selmon->cam.zoom : 1.0f;
	if (selmon) {
		selmon->cam.animating = 0;
		selmon->cam.coasting = 0;
	}
}

static void
gesture_pinch_update(struct wl_listener *listener, void *data)
{
	struct GestureDevice *g = wl_container_of(listener, g, pinch_update);
	struct wlr_pointer_pinch_update_event *event = data;
	float z;

	if (!g->pinch_active || event->scale <= 0.0 || !selmon)
		return;

	/* Same clamp range viewport_zoom() uses. */
	z = g->pinch_begin_zoom * (float)event->scale;
	if (z < 0.1f) z = 0.1f;
	if (z > 5.0f) z = 5.0f;
	selmon->cam.zoom = z;
	selmon->cam.target_zoom = z;

	viewport_camera_tick(selmon);
	status_mark_dirty();
}

static void
gesture_device_destroy(struct wl_listener *listener, void *data)
{
	struct GestureDevice *g = wl_container_of(listener, g, destroy);
	(void)data;
	wl_list_remove(&g->swipe_begin.link);
	wl_list_remove(&g->swipe_update.link);
	wl_list_remove(&g->swipe_end.link);
	wl_list_remove(&g->pinch_begin.link);
	wl_list_remove(&g->pinch_update.link);
	wl_list_remove(&g->destroy.link);
	free(g);
}

/* Called from createpointer() (dwl.c) for every pointer device, touchpad or
 * not — a plain mouse simply never emits swipe/pinch events, so this is a
 * harmless no-op set of listeners for it. */
void
gestures_attach(struct wlr_pointer *pointer)
{
	struct GestureDevice *g = ecalloc(1, sizeof(*g));

	LISTEN(&pointer->events.swipe_begin, &g->swipe_begin, gesture_swipe_begin);
	LISTEN(&pointer->events.swipe_update, &g->swipe_update, gesture_swipe_update);
	LISTEN(&pointer->events.swipe_end, &g->swipe_end, gesture_swipe_end);
	LISTEN(&pointer->events.pinch_begin, &g->pinch_begin, gesture_pinch_begin);
	LISTEN(&pointer->events.pinch_update, &g->pinch_update, gesture_pinch_update);
	LISTEN(&pointer->base.events.destroy, &g->destroy, gesture_device_destroy);
}
