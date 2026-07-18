/* Off-screen window indicators (edge markers), one set per monitor
 * (multi-camera): each monitor shows markers for its *own* held windows
 * (c->mon) that its *own* camera has panned away from, positioned within
 * that monitor's box (m->m) — not one global set against the whole layout
 * (sgeom), which under per-monitor cameras would light up every screen for
 * a window only one of them lost.
 *
 * Monitor create/destroy can't be hooked from this module (dwl.c owns the
 * lifecycle and this file may not add hooks there), so sets are created
 * lazily and garbage-collected by membership check against `mons` on each
 * update — a freed Monitor's pointer is only ever compared, never
 * dereferenced. */

typedef struct {
	struct wl_list link;
	Monitor *mon;
	struct wlr_scene_tree *tree;
	struct wlr_scene_rect *up;
	struct wlr_scene_rect *down;
	struct wlr_scene_rect *left;
	struct wlr_scene_rect *right;
	struct wlr_box box; /* mon->m as of the last (re)position */
} IndicatorSet;

static struct {
	struct wl_list sets; /* IndicatorSet.link */
	int enabled;
	int size;
	int margin;
} offscreen_indicators = {0};

static void
indicator_set_visible(struct wlr_scene_rect *rect, int visible)
{
	if (!rect)
		return;
	/* Called every frame from rendermon(); skip the call when the visibility
	 * hasn't actually changed instead of re-asserting it unconditionally. */
	if (rect->node.enabled == !!visible)
		return;
	wlr_scene_node_set_enabled(&rect->node, visible);
}

static void
indicator_set_destroy(IndicatorSet *set)
{
	if (!set)
		return;
	wl_list_remove(&set->link);
	if (set->tree)
		wlr_scene_node_destroy(&set->tree->node);
	free(set);
}

static IndicatorSet *
indicator_set_create(Monitor *m)
{
	IndicatorSet *set = ecalloc(1, sizeof(*set));

	set->mon = m;
	set->tree = wlr_scene_tree_create(layers[LyrOverlay]);
	if (!set->tree) {
		free(set);
		return NULL;
	}
	set->up = wlr_scene_rect_create(set->tree,
		offscreen_indicators.size, offscreen_indicators.size,
		offscreen_indicator_color);
	set->down = wlr_scene_rect_create(set->tree,
		offscreen_indicators.size, offscreen_indicators.size,
		offscreen_indicator_color);
	set->left = wlr_scene_rect_create(set->tree,
		offscreen_indicators.size, offscreen_indicators.size,
		offscreen_indicator_color);
	set->right = wlr_scene_rect_create(set->tree,
		offscreen_indicators.size, offscreen_indicators.size,
		offscreen_indicator_color);
	if (!set->up || !set->down || !set->left || !set->right) {
		indicator_set_destroy(set);
		return NULL;
	}

	indicator_set_visible(set->up, 0);
	indicator_set_visible(set->down, 0);
	indicator_set_visible(set->left, 0);
	indicator_set_visible(set->right, 0);

	wl_list_insert(&offscreen_indicators.sets, &set->link);
	return set;
}

static IndicatorSet *
indicator_set_get(Monitor *m)
{
	IndicatorSet *set;

	wl_list_for_each(set, &offscreen_indicators.sets, link) {
		if (set->mon == m)
			return set;
	}
	return indicator_set_create(m);
}

/* Pin the four markers to the edges of this set's monitor box. Cheap enough
 * to be safe every frame, but only actually repositions when mon->m changed
 * (output add/remove/mode change reshuffles the layout). */
static void
indicator_set_position(IndicatorSet *set)
{
	struct wlr_box *b = &set->mon->m;
	int size = offscreen_indicators.size;
	int margin = offscreen_indicators.margin;
	int cx, cy;

	if (set->box.x == b->x && set->box.y == b->y
			&& set->box.width == b->width && set->box.height == b->height)
		return;
	set->box = *b;

	if (size < 4)
		size = 4;
	if (margin < 0)
		margin = 0;

	cx = b->x + b->width / 2 - size / 2;
	cy = b->y + b->height / 2 - size / 2;

	wlr_scene_node_set_position(&set->up->node, cx, b->y + margin);
	wlr_scene_node_set_position(&set->down->node,
		cx, b->y + b->height - margin - size);
	wlr_scene_node_set_position(&set->left->node, b->x + margin, cy);
	wlr_scene_node_set_position(&set->right->node,
		b->x + b->width - margin - size, cy);
}

static void
indicator_set_update(IndicatorSet *set)
{
	Monitor *m = set->mon;
	Client *c;
	int show_up = 0;
	int show_down = 0;
	int show_left = 0;
	int show_right = 0;
	int lx, ly;
	int screen_w, screen_h;

	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m))
			continue;
		/* Camera-bypassed clients (fullscreen/maximized/docked, see
		 * client_apply_zoom_frame()) are glued to a screen-space rect on
		 * their monitor — like panels below, never meaningfully
		 * "off-screen". */
		if (c->isfullscreen || c->ismaximized || c->docked)
			continue;
		/* Panels (c->ispanel) are always on-screen by construction (a fixed
		 * dock rect) — never meaningfully "off-screen", so skip them rather
		 * than have them (harmlessly, since they're never actually
		 * off-screen) walk through this every frame. */
		if (c->ispanel)
			continue;
		if (!c->scene || !wlr_scene_node_coords(&c->scene->node, &lx, &ly))
			continue;

		/* Scene coords are the on-screen position (already through this
		 * monitor's camera), but c->geom is world-sized — the on-screen
		 * footprint scales with the camera's zoom. */
		screen_w = (int)lroundf(c->geom.width * MON_ZOOM_SAFE(m));
		screen_h = (int)lroundf(c->geom.height * MON_ZOOM_SAFE(m));

		if (ly < m->m.y)
			show_up = 1;
		if (ly + screen_h > m->m.y + m->m.height)
			show_down = 1;
		if (lx < m->m.x)
			show_left = 1;
		if (lx + screen_w > m->m.x + m->m.width)
			show_right = 1;

		if (show_up && show_down && show_left && show_right)
			break;
	}

	indicator_set_visible(set->up, show_up);
	indicator_set_visible(set->down, show_down);
	indicator_set_visible(set->left, show_left);
	indicator_set_visible(set->right, show_right);
}

/* Kept for the updatemons() call site (dwl.c is out of this module's hands):
 * marker positions now derive from each monitor's own box lazily in
 * offscreen_indicators_update() — and at the point updatemons() calls this,
 * the per-monitor boxes haven't been refreshed yet anyway. */
void
offscreen_indicators_configure(int w, int h)
{
	(void)w;
	(void)h;
}

void
offscreen_indicators_init(void)
{
	wl_list_init(&offscreen_indicators.sets);
	offscreen_indicators.enabled = offscreen_indicator_enabled;
	offscreen_indicators.size = offscreen_indicator_size;
	offscreen_indicators.margin = offscreen_indicator_margin;
}

void
offscreen_indicators_update(void)
{
	IndicatorSet *set, *tmp;
	Monitor *m;

	if (!offscreen_indicators.enabled)
		return;
	if (!offscreen_indicators.sets.next) /* init() not run yet */
		return;

	/* GC sets whose monitor has been destroyed (cleanupmon() frees the
	 * Monitor; only the stale pointer is compared here) — or disabled
	 * (closemon() zeroes m->m, and stale markers left at the old layout
	 * coords could land on whichever live monitor takes that region over).
	 * A re-enabled monitor just gets a fresh set on the next pass. */
	wl_list_for_each_safe(set, tmp, &offscreen_indicators.sets, link) {
		int alive = 0;

		wl_list_for_each(m, &mons, link) {
			if (m == set->mon) {
				alive = m->wlr_output && m->wlr_output->enabled
					&& m->m.width > 0 && m->m.height > 0;
				break;
			}
		}
		if (!alive)
			indicator_set_destroy(set);
	}

	wl_list_for_each(m, &mons, link) {
		if (!m->wlr_output || !m->wlr_output->enabled)
			continue;
		if (m->m.width <= 0 || m->m.height <= 0)
			continue;
		set = indicator_set_get(m);
		if (!set)
			continue;
		indicator_set_position(set);
		indicator_set_update(set);
	}
}
