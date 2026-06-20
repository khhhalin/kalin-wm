/* Lightweight compositor-side overlay clock (HH:MM, bottom-right). */
/* MOVED: see modules/ui/overlay_clock.c */

#define OVERLAY_CLOCK_DIGITS 4
#define OVERLAY_CLOCK_SEGS   7

/* Segment index order: a, b, c, d, e, f, g */
static const uint8_t overlay_clock_digit_masks[10] = {
	0x3f, /* 0: a b c d e f */
	0x06, /* 1: b c */
	0x5b, /* 2: a b d e g */
	0x4f, /* 3: a b c d g */
	0x66, /* 4: b c f g */
	0x6d, /* 5: a c d f g */
	0x7d, /* 6: a c d e f g */
	0x07, /* 7: a b c */
	0x7f, /* 8: a b c d e f g */
	0x6f  /* 9: a b c d f g */
};

static struct {
	struct wlr_scene_tree *tree;
	struct wlr_scene_rect *bg;
	struct wlr_scene_rect *segments[OVERLAY_CLOCK_DIGITS][OVERLAY_CLOCK_SEGS];
	struct wlr_scene_rect *colon[2];
	struct wl_event_source *timer;
	int enabled;
	int width;
	int height;
	int last_hour;
	int last_minute;
} overlay_clock = {0};

static void
overlay_clock_schedule_next_tick(void)
{
	struct timespec ts;
	int ms;
	int sec_in_minute;

	if (!overlay_clock.enabled || !overlay_clock.timer)
		return;

	clock_gettime(CLOCK_REALTIME, &ts);
	sec_in_minute = (int)(ts.tv_sec % 60);
	ms = (60 - sec_in_minute) * 1000 - (int)(ts.tv_nsec / 1000000L);
	if (ms < 250)
		ms = 250;
	wl_event_source_timer_update(overlay_clock.timer, ms);
}

static void
overlay_clock_set_digit(int digit_index, int value)
{
	int seg;
	uint8_t mask;

	if (digit_index < 0 || digit_index >= OVERLAY_CLOCK_DIGITS)
		return;
	if (value < 0 || value > 9)
		return;

	mask = overlay_clock_digit_masks[value];
	for (seg = 0; seg < OVERLAY_CLOCK_SEGS; seg++) {
		if (!overlay_clock.segments[digit_index][seg])
			continue;
		wlr_scene_node_set_enabled(
			&overlay_clock.segments[digit_index][seg]->node,
			(mask & (1u << seg)) != 0
		);
	}
}

static void
overlay_clock_update_time(void)
{
	time_t now;
	struct tm tm_now;
	int hour;
	int minute;

	if (!overlay_clock.enabled)
		return;

	now = time(NULL);
	if (now == (time_t)-1)
		return;
	if (!localtime_r(&now, &tm_now))
		return;

	hour = tm_now.tm_hour;
	minute = tm_now.tm_min;
	if (hour == overlay_clock.last_hour && minute == overlay_clock.last_minute)
		return;

	overlay_clock.last_hour = hour;
	overlay_clock.last_minute = minute;

	overlay_clock_set_digit(0, hour / 10);
	overlay_clock_set_digit(1, hour % 10);
	overlay_clock_set_digit(2, minute / 10);
	overlay_clock_set_digit(3, minute % 10);
}

static int
overlay_clock_timer_handler(void *data)
{
	(void)data;
	overlay_clock_update_time();
	overlay_clock_schedule_next_tick();
	return 0;
}

void
overlay_clock_configure(int w, int h)
{
	int x;
	int y;

	if (!overlay_clock.enabled || !overlay_clock.tree)
		return;
	if (w <= 0 || h <= 0)
		return;

	x = sgeom.x + w - overlay_clock_margin_px - overlay_clock.width;
	y = sgeom.y + h - overlay_clock_margin_px - overlay_clock.height;
	wlr_scene_node_set_position(&overlay_clock.tree->node, x, y);
}

void
overlay_clock_init(void)
{
	struct wlr_scene_tree *tree;
	struct wlr_scene_rect *r;
	int d;
	int t;
	int w;
	int h;
	int pad;
	int gap;
	int colon_gap;
	int colon_w;
	int x;
	int y;
	int seg_h;

	overlay_clock.enabled = overlay_clock_enabled;
	overlay_clock.last_hour = -1;
	overlay_clock.last_minute = -1;

	if (!overlay_clock.enabled)
		return;

	t = overlay_clock_segment_px;
	w = overlay_clock_digit_w;
	h = overlay_clock_digit_h;
	pad = overlay_clock_padding_px;
	gap = overlay_clock_digit_gap_px;
	colon_gap = gap;
	colon_w = t;

	if (t < 2) t = 2;
	if (w < (t * 3)) w = t * 3;
	if (h < (t * 5)) h = t * 5;
	if (pad < 0) pad = 0;
	if (gap < 2) gap = 2;
	if (colon_gap < 2) colon_gap = 2;
	if (colon_w < 2) colon_w = 2;

	overlay_clock.width = pad * 2 + (w * 4) + (gap * 2) + (colon_gap * 2) + colon_w;
	overlay_clock.height = pad * 2 + h;

	tree = wlr_scene_tree_create(layers[LyrOverlay]);
	overlay_clock.tree = tree;

	r = wlr_scene_rect_create(tree, overlay_clock.width, overlay_clock.height, overlay_clock_bg);
	overlay_clock.bg = r;

	x = pad;
	for (d = 0; d < OVERLAY_CLOCK_DIGITS; d++) {
		if (d == 2)
			x += colon_gap + colon_w + colon_gap;

		seg_h = (h - (3 * t)) / 2;
		if (seg_h < 1)
			seg_h = 1;

		/* a */
		r = wlr_scene_rect_create(tree, w - (2 * t), t, overlay_clock_fg);
		overlay_clock.segments[d][0] = r;
		wlr_scene_node_set_position(&r->node, x + t, pad);
		/* b */
		r = wlr_scene_rect_create(tree, t, seg_h, overlay_clock_fg);
		overlay_clock.segments[d][1] = r;
		wlr_scene_node_set_position(&r->node, x + w - t, pad + t);
		/* c */
		r = wlr_scene_rect_create(tree, t, seg_h, overlay_clock_fg);
		overlay_clock.segments[d][2] = r;
		wlr_scene_node_set_position(&r->node, x + w - t, pad + t + seg_h + t);
		/* d */
		r = wlr_scene_rect_create(tree, w - (2 * t), t, overlay_clock_fg);
		overlay_clock.segments[d][3] = r;
		wlr_scene_node_set_position(&r->node, x + t, pad + h - t);
		/* e */
		r = wlr_scene_rect_create(tree, t, seg_h, overlay_clock_fg);
		overlay_clock.segments[d][4] = r;
		wlr_scene_node_set_position(&r->node, x, pad + t + seg_h + t);
		/* f */
		r = wlr_scene_rect_create(tree, t, seg_h, overlay_clock_fg);
		overlay_clock.segments[d][5] = r;
		wlr_scene_node_set_position(&r->node, x, pad + t);
		/* g */
		r = wlr_scene_rect_create(tree, w - (2 * t), t, overlay_clock_fg);
		overlay_clock.segments[d][6] = r;
		y = pad + t + seg_h;
		wlr_scene_node_set_position(&r->node, x + t, y);

		x += w + gap;
		if (d == 1)
			x -= gap;
	}

	/* Colon (always on) */
	x = pad + (w * 2) + gap + colon_gap;
	seg_h = (h - (3 * t)) / 2;
	if (seg_h < 1)
		seg_h = 1;

	r = wlr_scene_rect_create(tree, colon_w, colon_w, overlay_clock_fg);
	overlay_clock.colon[0] = r;
	wlr_scene_node_set_position(&r->node, x, pad + t + (seg_h / 2) - (colon_w / 2));

	r = wlr_scene_rect_create(tree, colon_w, colon_w, overlay_clock_fg);
	overlay_clock.colon[1] = r;
	wlr_scene_node_set_position(&r->node, x, pad + t + seg_h + t + (seg_h / 2) - (colon_w / 2));

	overlay_clock_update_time();
	overlay_clock_configure(sgeom.width, sgeom.height);
	overlay_clock.timer = wl_event_loop_add_timer(event_loop, overlay_clock_timer_handler, NULL);
	overlay_clock_schedule_next_tick();
}
