/* wlr-foreign-toplevel-management-v1 integration.
 *
 * Exposes every managed toplevel to external Wayland clients (e.g. quickshell's
 * ToplevelManager) so a shell can build a taskbar / dock, drive window peek
 * thumbnails (combined with screencopy), and activate/close windows. This file
 * is #include'd into dwl.c and relies on its globals and forward declarations
 * (clients, mons, focustop, focusclient, arrange, setfullscreen,
 * client_send_close, client_get_title/appid, foreign_toplevel_mgr, LISTEN). */

static void
ftl_request_activate(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, foreign_activate);
	(void)data;
	if (!c || !c->mon)
		return;
	/* Focus the requested window and re-run the layout so the infinite-canvas
	 * camera can bring it into view. */
	focusclient(c, 1);
	arrange(c->mon);
}

static void
ftl_request_close(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, foreign_close);
	(void)data;
	if (c)
		client_send_close(c);
}

static void
ftl_request_fullscreen(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, foreign_fullscreen);
	struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
	if (c && event)
		setfullscreen(c, event->fullscreen);
}

static void
ftl_update_title(Client *c)
{
	const char *title;
	const char *appid;
	if (!c || !c->foreign_toplevel)
		return;
	title = client_get_title(c);
	appid = client_get_appid(c);
	wlr_foreign_toplevel_handle_v1_set_title(c->foreign_toplevel, title ? title : "");
	wlr_foreign_toplevel_handle_v1_set_app_id(c->foreign_toplevel, appid ? appid : "");
}

static void
ftl_create(Client *c)
{
	if (!foreign_toplevel_mgr || !c || c->foreign_toplevel)
		return;
	c->foreign_toplevel = wlr_foreign_toplevel_handle_v1_create(foreign_toplevel_mgr);
	if (!c->foreign_toplevel) {
		wlr_log(WLR_ERROR, "Failed to create foreign-toplevel handle");
		return;
	}
	c->foreign_toplevel->data = c;
	ftl_update_title(c);
	if (c->mon)
		wlr_foreign_toplevel_handle_v1_output_enter(c->foreign_toplevel,
				c->mon->wlr_output);
	LISTEN(&c->foreign_toplevel->events.request_activate,
			&c->foreign_activate, ftl_request_activate);
	LISTEN(&c->foreign_toplevel->events.request_close,
			&c->foreign_close, ftl_request_close);
	LISTEN(&c->foreign_toplevel->events.request_fullscreen,
			&c->foreign_fullscreen, ftl_request_fullscreen);
}

static void
ftl_destroy(Client *c)
{
	if (!c || !c->foreign_toplevel)
		return;
	wl_list_remove(&c->foreign_activate.link);
	wl_list_remove(&c->foreign_close.link);
	wl_list_remove(&c->foreign_fullscreen.link);
	wlr_foreign_toplevel_handle_v1_destroy(c->foreign_toplevel);
	c->foreign_toplevel = NULL;
}

/* Keep each handle's activated/fullscreen state in sync with the compositor.
 * Called from printstatus(), which already runs on every relevant change. */
static void
ftl_sync_state(void)
{
	Monitor *m;
	Client *c;
	Client *focused;
	if (!foreign_toplevel_mgr)
		return;
	wl_list_for_each(m, &mons, link) {
		focused = focustop(m);
		wl_list_for_each(c, &clients, link) {
			if (c->mon != m || !c->foreign_toplevel)
				continue;
			wlr_foreign_toplevel_handle_v1_set_activated(c->foreign_toplevel,
					c == focused);
			wlr_foreign_toplevel_handle_v1_set_fullscreen(c->foreign_toplevel,
					c->isfullscreen);
		}
	}
}
