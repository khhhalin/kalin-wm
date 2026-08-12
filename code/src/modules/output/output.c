/* Output / monitor management: creation, teardown, layout re-sync, and the
 * wlr-output-management-v1 + power-management + IPC configuration paths. Split
 * out of the dwl.c god-file (SRP); #include'd into dwl.c so it stays in the same
 * translation unit — the compositor globals it touches (mons, output_layout,
 * scene, layers, selmon, ...) and the callers' forward declarations all live in
 * dwl.c, so this is a pure relocation with no header/linkage changes. */

void
cleanupmon(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, destroy);
	LayerSurface *l, *tmp;
	size_t i;

	/* m->layers[i] are intentionally not unlinked */
	for (i = 0; i < LENGTH(m->layers); i++) {
		wl_list_for_each_safe(l, tmp, &m->layers[i], link)
			wlr_layer_surface_v1_destroy(l->layer_surface);
	}

	wl_list_remove(&m->destroy.link);
	wl_list_remove(&m->frame.link);
	wl_list_remove(&m->link);
	wl_list_remove(&m->request_state.link);
	if (m->lock_surface)
		destroylocksurface(&m->destroy_lock_surface, NULL);
	m->wlr_output->data = NULL;
	shaders_output_destroy(m);
	wlr_output_layout_remove(output_layout, m->wlr_output);
	wlr_scene_output_destroy(m->scene_output);

	closemon(m);
	wlr_scene_node_destroy(&m->fullscreen_bg->node);
	free(m);
}

void
closemon(Monitor *m)
{
	/* update selmon if needed and
	 * move closed monitor's clients to the focused one */
	Client *c;
	int i = 0, nmons = wl_list_length(&mons);
	if (!nmons) {
		selmon = NULL;
	} else if (m == selmon) {
		do /* don't switch to disabled mons */
			selmon = wl_container_of(mons.next, selmon, link);
		while (!selmon->wlr_output->enabled && i++ < nmons);

		if (!selmon->wlr_output->enabled)
			selmon = NULL;
	}

	wl_list_for_each(c, &clients, link) {
		if (c->geom.x > m->m.width)
			resize(c, (struct wlr_box){.x = c->geom.x - m->w.width, .y = c->geom.y,
					.width = c->geom.width, .height = c->geom.height}, 0);
		if (c->mon == m)
			setmon(c, selmon);
	}
	focus_top(selmon, 1);
	status_mark_dirty();
}

void
createmon(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new output (aka a display or
	 * monitor) becomes available. */
	struct wlr_output *wlr_output = data;
	const MonitorRule *r;
	size_t i;
	struct wlr_output_state state;
	Monitor *m;

	if (!wlr_output_init_render(wlr_output, alloc, drw))
		return;

	m = wlr_output->data = ecalloc(1, sizeof(*m));
	m->wlr_output = wlr_output;
	m->cam = cam_defaults; /* fresh independent camera (multi-camera) */

	/* Restore this output's saved camera (pan + zoom) over the defaults, so a
	 * restart returns to the exact view it had at the last save instead of
	 * snapping to origin. wlr_output->name is already populated here (the
	 * monrules loop below keys on it too). The respawned windows below don't
	 * yank the camera back: mapnotify() skips follow-new for any client whose
	 * geometry was restored from the same save. Set the animation target too,
	 * or viewport_tick() would ease straight back off the restored spot. */
	{
		float saved_x, saved_y, saved_zoom;
		if (persistence_camera_for_output(wlr_output->name, &saved_x, &saved_y, &saved_zoom)) {
			m->cam.x = m->cam.target_x = saved_x;
			m->cam.y = m->cam.target_y = saved_y;
			m->cam.zoom = m->cam.target_zoom = saved_zoom;
		}
	}

	for (i = 0; i < LENGTH(m->layers); i++)
		wl_list_init(&m->layers[i]);

	wlr_output_state_init(&state);
	/* Initialize monitor state using configured rules */
	for (r = monrules; r < END(monrules); r++) {
		if (!r->name || strstr(wlr_output->name, r->name)) {
			m->m.x = r->x;
			m->m.y = r->y;
			wlr_output_state_set_scale(&state, r->scale);
			wlr_output_state_set_transform(&state, r->rr);
			break;
		}
	}

	/* The mode is a tuple of (width, height, refresh rate), and each
	 * monitor supports only a specific set of modes. We just pick the
	 * monitor's preferred mode; a more sophisticated compositor would let
	 * the user configure it. */
	wlr_output_state_set_mode(&state, wlr_output_preferred_mode(wlr_output));

	/* Set up event listeners */
	LISTEN(&wlr_output->events.frame, &m->frame, rendermon);
	LISTEN(&wlr_output->events.destroy, &m->destroy, cleanupmon);
	LISTEN(&wlr_output->events.request_state, &m->request_state, requestmonstate);

	wlr_output_state_set_enabled(&state, 1);
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	wl_list_insert(&mons, &m->link);
	status_mark_dirty();

	/* The xdg-protocol specifies:
	 *
	 * If the fullscreened surface is not opaque, the compositor must make
	 * sure that other screen content not part of the same surface tree (made
	 * up of subsurfaces, popups or similarly coupled surfaces) are not
	 * visible below the fullscreened surface.
	 *
	 */
	/* updatemons() will resize and set correct position */
	m->fullscreen_bg = wlr_scene_rect_create(layers[LyrFS], 0, 0, fullscreen_bg);
	wlr_scene_node_set_enabled(&m->fullscreen_bg->node, 0);

	/* Adds this to the output layout in the order it was configured.
	 *
	 * The output layout utility automatically adds a wl_output global to the
	 * display, which Wayland clients can see to find out information about the
	 * output (such as DPI, scale factor, manufacturer, etc).
	 */
	m->scene_output = wlr_scene_output_create(scene, wlr_output);
	if (m->m.x == -1 && m->m.y == -1)
		wlr_output_layout_add_auto(output_layout, wlr_output);
	else
		wlr_output_layout_add(output_layout, wlr_output, m->m.x, m->m.y);
}

void
outputmgrapply(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, 0);
}

void
outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test)
{
	/*
	 * Called when a client such as wlr-randr requests a change in output
	 * configuration. This is only one way that the layout can be changed,
	 * so any Monitor information should be updated by updatemons() after an
	 * output_layout.change event, not here.
	 */
	struct wlr_output_configuration_head_v1 *config_head;
	int ok = 1;

	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;
		Monitor *m = wlr_output->data;
		struct wlr_output_state state;

		/* Ensure displays previously disabled by wlr-output-power-management-v1
		 * are properly handled*/
		m->asleep = 0;

		wlr_output_state_init(&state);
		wlr_output_state_set_enabled(&state, config_head->state.enabled);
		if (!config_head->state.enabled)
			goto apply_or_test;

		if (config_head->state.mode)
			wlr_output_state_set_mode(&state, config_head->state.mode);
		else
			wlr_output_state_set_custom_mode(&state,
					config_head->state.custom_mode.width,
					config_head->state.custom_mode.height,
					config_head->state.custom_mode.refresh);

		wlr_output_state_set_transform(&state, config_head->state.transform);
		wlr_output_state_set_scale(&state, config_head->state.scale);
		wlr_output_state_set_adaptive_sync_enabled(&state,
				config_head->state.adaptive_sync_enabled);

apply_or_test:
		ok &= test ? wlr_output_test_state(wlr_output, &state)
				: wlr_output_commit_state(wlr_output, &state);

		/* Don't move monitors if position wouldn't change. This avoids
		 * wlroots marking the output as manually configured.
		 * wlr_output_layout_add does not like disabled outputs */
		if (!test && wlr_output->enabled && (m->m.x != config_head->state.x || m->m.y != config_head->state.y))
			wlr_output_layout_add(output_layout, wlr_output,
					config_head->state.x, config_head->state.y);

		wlr_output_state_finish(&state);
	}

	if (ok)
		wlr_output_configuration_v1_send_succeeded(config);
	else
		wlr_output_configuration_v1_send_failed(config);
	wlr_output_configuration_v1_destroy(config);

	/* https://codeberg.org/dwl/dwl/issues/577 */
	updatemons(NULL, NULL);
}

void
outputmgrtest(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, 1);
}

Monitor *
monitor_find_by_name(const char *name)
{
	Monitor *m;

	if (!name)
		return NULL;
	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output && m->wlr_output->name
				&& strcmp(m->wlr_output->name, name) == 0)
			return m;
	}
	return NULL;
}

/* The IPC equivalent of what outputmgrapplyortest() does per-head for an
 * external wlr-output-management-v1 client (e.g. wlr-randr) — same
 * underlying wlr_output_state/commit path, just addressed by output name
 * from a plain-text IPC command instead of iterating a client-supplied
 * wlr_output_configuration_v1. width/height <= 0 leaves the mode unchanged;
 * scale <= 0 leaves the scale unchanged — a caller that only wants to
 * reposition or disable an output doesn't have to already know its current
 * mode/scale just to pass them through unmodified.
 *
 * Returns 1 on success, 0 if the output wasn't found or the commit failed. */
int
ipc_set_output(const char *name, int width, int height, float refresh,
		float scale, int x, int y, int enabled)
{
	Monitor *m = monitor_find_by_name(name);
	struct wlr_output_state state;
	struct wlr_output_mode *mode, *matched = NULL;
	int ok;

	if (!m)
		return 0;

	/* Ensure a display previously disabled by wlr-output-power-management-v1
	 * is properly handled, mirroring outputmgrapplyortest() above. */
	m->asleep = 0;

	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, enabled);
	if (enabled) {
		if (width > 0 && height > 0) {
			int32_t refresh_mhz = (int32_t)(refresh * 1000.0f + 0.5f);
			/* Prefer one of the output's own advertised modes over a
			 * synthesized one, same preference outputmgrapplyortest() gives
			 * a real client-supplied mode over its custom_mode fallback —
			 * an exact match lets the monitor use its native timings
			 * instead of whatever the caller guessed at. refresh<=0 (caller
			 * doesn't care) matches any refresh at that resolution. */
			wl_list_for_each(mode, &m->wlr_output->modes, link) {
				if (mode->width == width && mode->height == height
						&& (refresh <= 0.0f || mode->refresh == refresh_mhz)) {
					matched = mode;
					break;
				}
			}
			if (matched)
				wlr_output_state_set_mode(&state, matched);
			else
				wlr_output_state_set_custom_mode(&state, width, height, refresh_mhz);
		}
		if (scale > 0.0f)
			wlr_output_state_set_scale(&state, scale);
	}

	ok = wlr_output_commit_state(m->wlr_output, &state);
	wlr_output_state_finish(&state);

	/* Don't move monitors if position wouldn't change — see
	 * outputmgrapplyortest()'s matching comment for why (avoids wlroots
	 * marking the output as manually configured, and wlr_output_layout_add
	 * dislikes disabled outputs). */
	if (ok && m->wlr_output->enabled && (m->m.x != x || m->m.y != y))
		wlr_output_layout_add(output_layout, m->wlr_output, x, y);

	updatemons(NULL, NULL);
	return ok;
}

void
powermgrsetmode(struct wl_listener *listener, void *data)
{
	struct wlr_output_power_v1_set_mode_event *event = data;
	struct wlr_output_state state = {0};
	Monitor *m = event->output->data;

	if (!m)
		return;

	m->gamma_lut_changed = 1; /* Reapply gamma LUT when re-enabling the output */
	wlr_output_state_set_enabled(&state, event->mode);
	wlr_output_commit_state(m->wlr_output, &state);

	m->asleep = !event->mode;
	updatemons(NULL, NULL);
}

void
requestmonstate(struct wl_listener *listener, void *data)
{
	struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(event->output, event->state);
	updatemons(NULL, NULL);
}

void
updatemons(struct wl_listener *listener, void *data)
{
	/*
	 * Called whenever the output layout changes: adding or removing a
	 * monitor, changing an output's mode or position, etc. This is where
	 * the change officially happens and we update geometry, window
	 * positions, focus, and the stored configuration in wlroots'
	 * output-manager implementation.
	 */
	struct wlr_output_configuration_v1 *config
			= wlr_output_configuration_v1_create();
	Client *c;
	struct wlr_output_configuration_head_v1 *config_head;
	Monitor *m;

	/* First remove from the layout the disabled monitors */
	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output->enabled || m->asleep)
			continue;
		config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);
		config_head->state.enabled = 0;
		/* Remove this output from the layout to avoid cursor enter inside it */
		wlr_output_layout_remove(output_layout, m->wlr_output);
		closemon(m);
		m->m = m->w = (struct wlr_box){0};
	}
	/* Insert outputs that need to */
	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output->enabled
				&& !wlr_output_layout_get(output_layout, m->wlr_output))
			wlr_output_layout_add_auto(output_layout, m->wlr_output);
	}

	/* Now that we update the output layout we can get its box */
	wlr_output_layout_get_box(output_layout, NULL, &sgeom);

	wlr_scene_node_set_position(&root_bg->node, sgeom.x, sgeom.y);
	wlr_scene_rect_set_size(root_bg, sgeom.width, sgeom.height);

	wallpaper_configure(sgeom.width, sgeom.height);
	offscreen_indicators_configure(sgeom.width, sgeom.height);

	/* Make sure the clients are hidden when dwl is locked */
	session_lock_resize();

	wl_list_for_each(m, &mons, link) {
		if (!m->wlr_output->enabled)
			continue;
		config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);

		/* Get the effective monitor geometry to use for surfaces */
		wlr_output_layout_get_box(output_layout, m->wlr_output, &m->m);
		m->w = m->m;
		wlr_scene_output_set_position(m->scene_output, m->m.x, m->m.y);

		wlr_scene_node_set_position(&m->fullscreen_bg->node, m->m.x, m->m.y);
		wlr_scene_rect_set_size(m->fullscreen_bg, m->m.width, m->m.height);

		session_lock_configure_output(m);

		/* Calculate the effective monitor geometry to use for clients */
		arrangelayers(m);
		/* Don't move clients to the left output when plugging monitors */
		arrange_mark_dirty(m);
		/* make sure fullscreen clients have the right size */
		if ((c = focustop(m)) && c->isfullscreen)
			resize(c, m->m, 0);

		/* Try to re-set the gamma LUT when updating monitors,
		 * it's only really needed when enabling a disabled output, but meh. */
		m->gamma_lut_changed = 1;

		config_head->state.x = m->m.x;
		config_head->state.y = m->m.y;

		if (!selmon) {
			selmon = m;
		}
	}

	if (selmon && selmon->wlr_output->enabled) {
		wl_list_for_each(c, &clients, link) {
			if (!c->mon && client_surface(c)->mapped)
				setmon(c, selmon);
		}
		focus_top(selmon, 1);
		if (selmon->lock_surface) {
			client_notify_enter(selmon->lock_surface->surface,
					wlr_seat_get_keyboard(seat));
			client_activate_surface(selmon->lock_surface->surface, 1);
		}
	}

	/* Re-anchor the drawn cursor after output changes: a DPMS wake modeset
	 * resets the backend's cursor plane to 0,0 while cursor->x/y stay
	 * correct. The old wlr_cursor_move(cursor, NULL, 0, 0) never repaired
	 * this because wlr_output_cursor_move() early-returns when its cached
	 * position is unchanged, so nothing reached the hardware. Bounce through
	 * the layout origin and back so the second warp is a real move that
	 * re-commits the plane at the true position; like the old call, none of
	 * this sends motion events to clients. */
	{
		double cursor_x = cursor->x, cursor_y = cursor->y;
		wlr_cursor_warp_closest(cursor, NULL, 0, 0);
		wlr_cursor_warp_closest(cursor, NULL, cursor_x, cursor_y);
	}

	wlr_output_manager_v1_set_configuration(output_mgr, config);
}
