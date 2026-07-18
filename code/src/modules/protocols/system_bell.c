/* xdg-system-bell-v1: lets a client ring the system bell (terminal \a etc.)
 * through the compositor. kalin-wm has no audible/visual bell to drive, but
 * advertising the global stops clients from logging "no such interface"
 * every session; the ring handler exists so a future urgency-hint or
 * visual-bell feature has the event already plumbed, and so a bell can be
 * seen in the debug log when diagnosing a chatty client. */
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_system_bell_v1.h>
#include <wlr/util/log.h>

static void
handle_ring(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_system_bell_v1_ring_event *event = data;

	if (!event)
		return;
	wlr_log(WLR_DEBUG, "system_bell: ring (surface=%p)", (void *)event->surface);
}

static struct wl_listener ring_listener = {.notify = handle_ring};

void
system_bell_init(struct wl_display *display)
{
	struct wlr_xdg_system_bell_v1 *bell;

	bell = wlr_xdg_system_bell_v1_create(display, 1);
	if (!bell) {
		wlr_log(WLR_ERROR, "system_bell: create failed");
		return;
	}
	wl_signal_add(&bell->events.ring, &ring_listener);
}
