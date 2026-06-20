/* Minimal Wayland client: connect to $WAYLAND_DISPLAY, dump the global
 * registry, and exit non-zero unless the requested interface is present.
 * Used to verify kalin-wm advertises zwlr_foreign_toplevel_manager_v1. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wayland-client.h>

static const char *want;
static int found;

static void
handle_global(void *data, struct wl_registry *reg, uint32_t name,
		const char *iface, uint32_t version)
{
	(void)data; (void)reg; (void)name;
	printf("  %-45s v%u\n", iface, version);
	if (want && strcmp(iface, want) == 0)
		found = 1;
}

static void
handle_global_remove(void *data, struct wl_registry *reg, uint32_t name)
{
	(void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

int
main(int argc, char *argv[])
{
	struct wl_display *dpy;
	struct wl_registry *reg;

	want = argc > 1 ? argv[1] : NULL;
	dpy = wl_display_connect(NULL);
	if (!dpy) {
		fprintf(stderr, "registry_probe: cannot connect to WAYLAND_DISPLAY\n");
		return 2;
	}
	reg = wl_display_get_registry(dpy);
	wl_registry_add_listener(reg, &registry_listener, NULL);
	wl_display_roundtrip(dpy);
	wl_display_roundtrip(dpy);
	wl_display_disconnect(dpy);

	if (want) {
		printf("%s: %s\n", want, found ? "PRESENT" : "ABSENT");
		return found ? 0 : 1;
	}
	return 0;
}
