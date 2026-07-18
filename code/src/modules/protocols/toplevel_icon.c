/* xdg-toplevel-icon-v1: lets a client provide its own taskbar/alt-tab icon
 * (a named icon and/or pixel buffers) instead of the compositor guessing
 * from .desktop lookup. Implemented via the wlroots 0.20 wrapper — before
 * this module, clients requesting the interface got a per-session "no such
 * global" warning in the log and fell back to nothing.
 *
 * The module keeps its own toplevel->icon map instead of adding a field to
 * the shared Client struct: kalin.h/dwl.c duplicate that struct definition,
 * so struct edits are a keeper-level change, and no in-tree consumer exists
 * yet anyway. The map (and the ref taken on each icon) is still required
 * for correctness, not just future use — the wlroots header is explicit
 * that a set_icon event's icon is only valid past the event if referenced,
 * so a compositor that wants to remember "this window's current icon" for
 * the planned Quickshell taskbar hand-off must ref it here and unref on
 * replacement or toplevel destroy.
 *
 * Separately-compiled TU: needs nothing from dwl.c beyond the one
 * toplevel_icon_init(dpy) call in setup(). */
#include <stdlib.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_toplevel_icon_v1.h>
#include <wlr/util/log.h>

typedef struct {
	struct wlr_xdg_toplevel *toplevel;
	struct wlr_xdg_toplevel_icon_v1 *icon; /* ref held by this module */
	struct wl_listener toplevel_destroy;
	struct wl_list link;                   /* icons */
} ToplevelIcon;

static struct wl_list icons;

static void
toplevel_icon_drop(ToplevelIcon *entry)
{
	if (!entry)
		return;
	if (entry->icon)
		wlr_xdg_toplevel_icon_v1_unref(entry->icon);
	wl_list_remove(&entry->toplevel_destroy.link);
	wl_list_remove(&entry->link);
	free(entry);
}

static void
handle_toplevel_destroy(struct wl_listener *listener, void *data)
{
	ToplevelIcon *entry = wl_container_of(listener, entry, toplevel_destroy);
	toplevel_icon_drop(entry);
}

static ToplevelIcon *
toplevel_icon_find(struct wlr_xdg_toplevel *toplevel)
{
	ToplevelIcon *entry;
	wl_list_for_each(entry, &icons, link) {
		if (entry->toplevel == toplevel)
			return entry;
	}
	return NULL;
}

static void
handle_set_icon(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_icon_manager_v1_set_icon_event *event = data;
	ToplevelIcon *entry;

	if (!event || !event->toplevel)
		return;

	entry = toplevel_icon_find(event->toplevel);

	/* A NULL icon is the protocol's "remove my icon" request. */
	if (!event->icon) {
		toplevel_icon_drop(entry);
		return;
	}

	if (!entry) {
		entry = calloc(1, sizeof(*entry));
		if (!entry) {
			wlr_log(WLR_ERROR, "toplevel_icon: entry allocation failed");
			return;
		}
		entry->toplevel = event->toplevel;
		entry->toplevel_destroy.notify = handle_toplevel_destroy;
		wl_signal_add(&event->toplevel->events.destroy, &entry->toplevel_destroy);
		wl_list_insert(&icons, &entry->link);
	}

	if (entry->icon)
		wlr_xdg_toplevel_icon_v1_unref(entry->icon);
	entry->icon = wlr_xdg_toplevel_icon_v1_ref(event->icon);

	wlr_log(WLR_DEBUG, "toplevel_icon: icon set for '%s' (name=%s)",
			event->toplevel->app_id ? event->toplevel->app_id : "?",
			entry->icon->name ? entry->icon->name : "<buffers only>");
}

static struct wl_listener set_icon_listener = {.notify = handle_set_icon};

void
toplevel_icon_init(struct wl_display *display)
{
	struct wlr_xdg_toplevel_icon_manager_v1 *manager;

	wl_list_init(&icons);
	manager = wlr_xdg_toplevel_icon_manager_v1_create(display, 1);
	if (!manager) {
		wlr_log(WLR_ERROR, "toplevel_icon: manager create failed");
		return;
	}
	/* No wlr_xdg_toplevel_icon_manager_v1_set_sizes() call: an empty size
	 * list means "no preference", which is honest until the Quickshell
	 * taskbar consumer exists and dictates real sizes. */
	wl_signal_add(&manager->events.set_icon, &set_icon_listener);
}
