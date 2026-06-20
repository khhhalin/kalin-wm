/* Viewport camera operations: pan, zoom, follow, and animation tick. */
/* MOVED: see modules/viewport/viewport_ops.c */

static void
viewport_move_to(float x, float y, int smooth)
{
	viewport.target_x = x;
	viewport.target_y = y;

	if (smooth && viewport.smooth_pan) {
		viewport.animating = 1;
	} else {
		viewport.x = x;
		viewport.y = y;
		viewport.animating = 0;
	}
}

void
viewport_tick(void)
{
	float dx, dy;
	float alpha;

	if (!viewport.animating || !selmon)
		return;

	dx = viewport.target_x - viewport.x;
	dy = viewport.target_y - viewport.y;

	if (fabsf(dx) < 0.5f && fabsf(dy) < 0.5f) {
		viewport.x = viewport.target_x;
		viewport.y = viewport.target_y;
		viewport.animating = 0;
		arrange(selmon);
		return;
	}

	alpha = 0.22f;
	viewport.x += dx * alpha;
	viewport.y += dy * alpha;
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
	if (factor <= 0.0f)
		return;
	
	viewport.zoom *= factor;
	
	/* Clamp zoom range */
	if (viewport.zoom < 0.1f)
		viewport.zoom = 0.1f;
	if (viewport.zoom > 5.0f)
		viewport.zoom = 5.0f;
	
	wlr_log(WLR_DEBUG, "Viewport zoom: %.2f", viewport.zoom);
	
	if (selmon)
		arrange(selmon);
	
	printstatus();
}

void
viewport_reset(const Arg *arg)
{
	(void)arg; /* unused */
	
	viewport_move_to(0.0f, 0.0f, 0);
	viewport.zoom = 1.0f;
	
	if (selmon)
		arrange(selmon);
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
