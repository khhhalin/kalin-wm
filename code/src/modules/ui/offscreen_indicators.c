/* Off-screen window indicators (edge markers). */

static struct {
	struct wlr_scene_tree *tree;
	struct wlr_scene_rect *up;
	struct wlr_scene_rect *down;
	struct wlr_scene_rect *left;
	struct wlr_scene_rect *right;
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
offscreen_indicators_apply_visibility(int show_up, int show_down, int show_left, int show_right)
{
	indicator_set_visible(offscreen_indicators.up, show_up);
	indicator_set_visible(offscreen_indicators.down, show_down);
	indicator_set_visible(offscreen_indicators.left, show_left);
	indicator_set_visible(offscreen_indicators.right, show_right);
}

void
offscreen_indicators_configure(int w, int h)
{
	int size;
	int margin;
	int cx;
	int cy;

	if (!offscreen_indicators.enabled || !offscreen_indicators.tree)
		return;
	if (w <= 0 || h <= 0)
		return;

	size = offscreen_indicators.size;
	margin = offscreen_indicators.margin;
	if (size < 4)
		size = 4;
	if (margin < 0)
		margin = 0;

	cx = sgeom.x + w / 2 - size / 2;
	cy = sgeom.y + h / 2 - size / 2;

	wlr_scene_node_set_position(&offscreen_indicators.up->node,
		cx, sgeom.y + margin);
	wlr_scene_node_set_position(&offscreen_indicators.down->node,
		cx, sgeom.y + h - margin - size);
	wlr_scene_node_set_position(&offscreen_indicators.left->node,
		sgeom.x + margin, cy);
	wlr_scene_node_set_position(&offscreen_indicators.right->node,
		sgeom.x + w - margin - size, cy);
}

void
offscreen_indicators_init(void)
{
	struct wlr_scene_tree *tree;

	offscreen_indicators.enabled = offscreen_indicator_enabled;
	offscreen_indicators.size = offscreen_indicator_size;
	offscreen_indicators.margin = offscreen_indicator_margin;

	if (!offscreen_indicators.enabled)
		return;

	tree = wlr_scene_tree_create(layers[LyrOverlay]);
	offscreen_indicators.tree = tree;

	offscreen_indicators.up = wlr_scene_rect_create(tree,
		offscreen_indicators.size, offscreen_indicators.size,
		offscreen_indicator_color);
	offscreen_indicators.down = wlr_scene_rect_create(tree,
		offscreen_indicators.size, offscreen_indicators.size,
		offscreen_indicator_color);
	offscreen_indicators.left = wlr_scene_rect_create(tree,
		offscreen_indicators.size, offscreen_indicators.size,
		offscreen_indicator_color);
	offscreen_indicators.right = wlr_scene_rect_create(tree,
		offscreen_indicators.size, offscreen_indicators.size,
		offscreen_indicator_color);

	offscreen_indicators_apply_visibility(0, 0, 0, 0);
	offscreen_indicators_configure(sgeom.width, sgeom.height);
}

void
offscreen_indicators_update(void)
{
	Client *c;
	int show_up = 0;
	int show_down = 0;
	int show_left = 0;
	int show_right = 0;
	int lx, ly;
	int right_edge;
	int bottom_edge;

	if (!offscreen_indicators.enabled || !offscreen_indicators.tree)
		return;

	wl_list_for_each(c, &clients, link) {
		if (!c->mon)
			continue;
		if (!VISIBLEON(c, c->mon) || c->isfullscreen)
			continue;
		/* Panels (c->ispanel) are always on-screen by construction (a fixed
		 * dock rect) — never meaningfully "off-screen", so skip them rather
		 * than have them (harmlessly, since they're never actually
		 * off-screen) walk through this every frame. */
		if (c->ispanel)
			continue;
		if (!wlr_scene_node_coords(&c->scene->node, &lx, &ly))
			continue;

		right_edge = lx + c->geom.width;
		bottom_edge = ly + c->geom.height;

		if (ly < sgeom.y)
			show_up = 1;
		if (bottom_edge > sgeom.y + sgeom.height)
			show_down = 1;
		if (lx < sgeom.x)
			show_left = 1;
		if (right_edge > sgeom.x + sgeom.width)
			show_right = 1;

		if (show_up && show_down && show_left && show_right)
			break;
	}

	offscreen_indicators_apply_visibility(show_up, show_down, show_left, show_right);
}
