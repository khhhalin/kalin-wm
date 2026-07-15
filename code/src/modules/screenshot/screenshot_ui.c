/* Niri-style interactive screenshot UI: Super+Shift+S opens a freeze-frame
 * of the focused monitor under a dim overlay, pre-selecting the whole
 * monitor; dragging draws a custom region. Keys match niri's screenshot UI
 * exactly: Escape cancels, Space/Enter confirms (disk + clipboard), Ctrl+C
 * confirms clipboard-only, P toggles pointer visibility.
 *
 * The frame is frozen at open (the world visibly stops, like niri) and the
 * confirm crops from those frozen pixels — WYSIWYG, and it keeps the UI's
 * own overlay/border/info nodes out of the saved image (the old re-render
 * path captured the dim overlay into every screenshot).
 *
 * A TUI-styled readout (selection size/position + key hints, warm-amber
 * palette matching the bar's kalin_tuis suite) is rasterized with a small
 * built-in 5x7 font into a pixel buffer — no font dependency.
 *
 * Visual style otherwise mirrors crop_mode.c's overlay (dark rect + bright
 * border in layers[LyrOverlay]).
 *
 * Separately-compiled TU: reads/writes the shared `screenshot_ui` state and
 * links against dwl.c's externed globals (selmon, layers, cursor, cursor_mgr)
 * via kalin.h. */
#include "kalin.h"
#include <wlr/interfaces/wlr_buffer.h>

/* Same fallback capture.c uses — the devshell's wlroots cflags don't
 * propagate libdrm's include dir, so <drm_fourcc.h> isn't reachable. */
#ifndef DRM_FORMAT_XRGB8888
#define DRM_FORMAT_XRGB8888 0x34325258
#endif

#define SCREENSHOT_OVERLAY_ALPHA 0.35f
#define SCREENSHOT_BORDER_WIDTH 2

/* Warm-amber palette, same sources as tools/bar-tuis/kalin_tuis/theme.py
 * (foot.ini background/foreground + the bar Theme.qml accent). */
#define INFO_COL_BG     0xFF1E1915u
#define INFO_COL_BORDER 0xFFF0A030u
#define INFO_COL_TEXT   0xFFE7D8BFu
#define INFO_COL_DIM    0xFFB08D5Fu
#define BORDER_COL_R    (0xF0 / 255.0f)
#define BORDER_COL_G    (0xA0 / 255.0f)
#define BORDER_COL_B    (0x30 / 255.0f)

#define INFO_SCALE   2   /* 5x7 glyphs drawn at 10x14 */
#define INFO_GLYPH_W 6   /* 5px glyph + 1px advance, pre-scale */
#define INFO_GLYPH_H 7
#define INFO_PAD     10
#define INFO_MARGIN  60  /* gap above the monitor's bottom edge (clears the bar) */

/* ── owned-pixels wlr_buffer ─────────────────────────────────────────────────
 * Minimal wlr_buffer wrapping a malloc'd XRGB8888 array; the buffer owns the
 * pixels and frees them on destroy, so scene/renderer lifetime (a texture
 * upload may outlive our scene node) can never use freed memory. */

struct pixel_buffer {
	struct wlr_buffer base;
	void *data;
	size_t stride;
};

static void
pixel_buffer_destroy(struct wlr_buffer *wlr_buf)
{
	struct pixel_buffer *buf = (struct pixel_buffer *)wlr_buf;
	free(buf->data);
	free(buf);
}

static bool
pixel_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buf, uint32_t flags,
		void **data, uint32_t *format, size_t *stride)
{
	struct pixel_buffer *buf = (struct pixel_buffer *)wlr_buf;
	if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE)
		return false;
	*data = buf->data;
	*format = DRM_FORMAT_XRGB8888;
	*stride = buf->stride;
	return true;
}

static void
pixel_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buf)
{
	(void)wlr_buf;
}

static const struct wlr_buffer_impl pixel_buffer_impl = {
	.destroy = pixel_buffer_destroy,
	.begin_data_ptr_access = pixel_buffer_begin_data_ptr_access,
	.end_data_ptr_access = pixel_buffer_end_data_ptr_access,
};

/* Takes ownership of `data` (freed on failure too). */
static struct wlr_buffer *
pixel_buffer_create(void *data, int w, int h, size_t stride)
{
	struct pixel_buffer *buf = calloc(1, sizeof(*buf));
	if (!buf) {
		free(data);
		return NULL;
	}
	wlr_buffer_init(&buf->base, &pixel_buffer_impl, w, h);
	buf->data = data;
	buf->stride = stride;
	return &buf->base;
}

/* ── built-in 5x7 font ───────────────────────────────────────────────────────
 * Hand-rolled uppercase/digits/symbols — enough for the readout; anything
 * else renders as blank. Each byte is one row, bit 4 = leftmost pixel. */

static const uint8_t font5x7_digits[10][7] = {
	{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
	{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
	{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
	{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, /* 3 */
	{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
	{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
	{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
	{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
	{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
	{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
};

static const uint8_t font5x7_upper[26][7] = {
	{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
	{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
	{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
	{0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, /* D */
	{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
	{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
	{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, /* G */
	{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
	{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
	{0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* J */
	{0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
	{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
	{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
	{0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
	{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
	{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
	{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
	{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
	{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* S */
	{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
	{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
	{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
	{0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, /* W */
	{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
	{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
	{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
};

static const uint8_t *
font5x7_glyph(char ch)
{
	static const uint8_t sym_lparen[7]  = {0x02,0x04,0x08,0x08,0x08,0x04,0x02};
	static const uint8_t sym_rparen[7]  = {0x08,0x04,0x02,0x02,0x02,0x04,0x08};
	static const uint8_t sym_comma[7]   = {0x00,0x00,0x00,0x00,0x06,0x04,0x08};
	static const uint8_t sym_plus[7]    = {0x00,0x04,0x04,0x1F,0x04,0x04,0x00};
	static const uint8_t sym_minus[7]   = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00};
	static const uint8_t sym_slash[7]   = {0x01,0x02,0x02,0x04,0x08,0x08,0x10};
	static const uint8_t sym_colon[7]   = {0x00,0x06,0x06,0x00,0x06,0x06,0x00};
	static const uint8_t sym_period[7]  = {0x00,0x00,0x00,0x00,0x00,0x06,0x06};

	if (ch >= '0' && ch <= '9')
		return font5x7_digits[ch - '0'];
	if (ch >= 'A' && ch <= 'Z')
		return font5x7_upper[ch - 'A'];
	if (ch >= 'a' && ch <= 'z')
		return font5x7_upper[ch - 'a'];
	switch (ch) {
	case '(': return sym_lparen;
	case ')': return sym_rparen;
	case ',': return sym_comma;
	case '+': return sym_plus;
	case '-': return sym_minus;
	case '/': return sym_slash;
	case ':': return sym_colon;
	case '.': return sym_period;
	default:  return NULL; /* incl. space: draw nothing */
	}
}

static void
info_draw_text(uint32_t *canvas, int canvas_w, int canvas_h, int x, int y,
		const char *str, uint32_t color)
{
	const char *p;
	const uint8_t *glyph;
	int row, col, sy, sx, px, py;

	for (p = str; *p; p++, x += INFO_GLYPH_W * INFO_SCALE) {
		glyph = font5x7_glyph(*p);
		if (!glyph)
			continue;
		for (row = 0; row < INFO_GLYPH_H; row++) {
			for (col = 0; col < 5; col++) {
				if (!(glyph[row] & (0x10 >> col)))
					continue;
				for (sy = 0; sy < INFO_SCALE; sy++) {
					for (sx = 0; sx < INFO_SCALE; sx++) {
						px = x + col * INFO_SCALE + sx;
						py = y + row * INFO_SCALE + sy;
						if (px >= 0 && px < canvas_w && py >= 0 && py < canvas_h)
							canvas[py * canvas_w + px] = color;
					}
				}
			}
		}
	}
}

/* ── UI ──────────────────────────────────────────────────────────────────── */

static void
screenshotui_info_destroy(void)
{
	if (screenshot_ui.info_node)
		wlr_scene_node_destroy(&screenshot_ui.info_node->node);
	screenshot_ui.info_node = NULL;
	if (screenshot_ui.info_buf)
		wlr_buffer_drop(screenshot_ui.info_buf);
	screenshot_ui.info_buf = NULL;
}

/* Rasterize the two-line readout (selection geometry + key hints) into a
 * fresh buffer and place it bottom-center. Cheap enough to redo per change:
 * the canvas is a few hundred px wide and only re-rendered when the text
 * actually differs (a drag changes it per motion event, hovering doesn't). */
static void
screenshotui_info_update(int x, int y, int w, int h)
{
	static const char *hints = "ENTER/SPACE SAVE   CTRL+C COPY   ESC CANCEL   P POINTER";
	char line[96];
	uint32_t *canvas;
	int len_line, len_hints, cols, box_w, box_h, ix, iy, px, py;
	Monitor *m = screenshot_ui.mon;

	if (!m)
		return;

	snprintf(line, sizeof(line), "%d X %d   AT (%d,%d)",
			w, h, x - (int)m->m.x, y - (int)m->m.y);
	if (strcmp(line, screenshot_ui.info_text) == 0 && screenshot_ui.info_node)
		return;
	snprintf(screenshot_ui.info_text, sizeof(screenshot_ui.info_text), "%s", line);

	len_line = (int)strlen(line);
	len_hints = (int)strlen(hints);
	cols = len_line > len_hints ? len_line : len_hints;
	box_w = cols * INFO_GLYPH_W * INFO_SCALE + 2 * INFO_PAD;
	box_h = 2 * INFO_GLYPH_H * INFO_SCALE + 3 * INFO_PAD;

	canvas = malloc((size_t)box_w * box_h * 4);
	if (!canvas)
		return;
	for (py = 0; py < box_h; py++)
		for (px = 0; px < box_w; px++)
			canvas[py * box_w + px] =
				(px < 2 || py < 2 || px >= box_w - 2 || py >= box_h - 2)
					? INFO_COL_BORDER : INFO_COL_BG;

	/* Selection line centered, hints below in the dim tier. */
	info_draw_text(canvas, box_w, box_h,
			(box_w - len_line * INFO_GLYPH_W * INFO_SCALE) / 2, INFO_PAD,
			line, INFO_COL_TEXT);
	info_draw_text(canvas, box_w, box_h,
			(box_w - len_hints * INFO_GLYPH_W * INFO_SCALE) / 2,
			2 * INFO_PAD + INFO_GLYPH_H * INFO_SCALE,
			hints, INFO_COL_DIM);

	screenshotui_info_destroy();
	screenshot_ui.info_buf = pixel_buffer_create(canvas, box_w, box_h,
			(size_t)box_w * 4);
	if (!screenshot_ui.info_buf) {
		wlr_log(WLR_ERROR, "screenshot UI: info pixel_buffer_create failed");
		return;
	}
	screenshot_ui.info_node = wlr_scene_buffer_create(layers[LyrOverlay],
			screenshot_ui.info_buf);
	if (!screenshot_ui.info_node) {
		wlr_log(WLR_ERROR, "screenshot UI: info scene_buffer_create failed");
		return;
	}
	ix = (int)m->m.x + ((int)m->m.width - box_w) / 2;
	iy = (int)m->m.y + (int)m->m.height - box_h - INFO_MARGIN;
	wlr_scene_node_set_position(&screenshot_ui.info_node->node, ix, iy);
}

static void
screenshotui_freeze(Monitor *m)
{
	unsigned char *data;
	int cw, ch;
	size_t stride;

	if (!capture_render_native(m, 1.0f, &data, &cw, &ch, &stride)) {
		wlr_log(WLR_ERROR, "screenshot UI: freeze-frame render failed, selecting live");
		return;
	}
	screenshot_ui.frozen_buf = pixel_buffer_create(data, cw, ch, stride);
	if (!screenshot_ui.frozen_buf)
		return;
	/* The buffer owns `data`; this alias is only read by confirm's export,
	 * strictly before the buffer is dropped. */
	screenshot_ui.frozen_data = data;
	screenshot_ui.frozen_w = cw;
	screenshot_ui.frozen_h = ch;
	screenshot_ui.frozen_stride = stride;

	screenshot_ui.frozen_node = wlr_scene_buffer_create(layers[LyrOverlay],
			screenshot_ui.frozen_buf);
	if (!screenshot_ui.frozen_node) {
		wlr_log(WLR_ERROR, "screenshot UI: freeze scene_buffer_create failed");
		return;
	}
	wlr_scene_node_set_position(&screenshot_ui.frozen_node->node,
			(int)m->m.x, (int)m->m.y);
	/* Native (possibly HiDPI) pixels displayed at the monitor's logical size. */
	wlr_scene_buffer_set_dest_size(screenshot_ui.frozen_node,
			m->m.width, m->m.height);
}

void
screenshotui_begin(const Arg *arg)
{
	Monitor *m;
	int i;
	(void)arg;

	if (!selmon || screenshot_ui.active)
		return;

	m = selmon;
	screenshot_ui.active = true;
	screenshot_ui.mon = m;
	screenshot_ui.dragging = false;
	screenshot_ui.show_pointer = false;
	screenshot_ui.info_text[0] = '\0';

	/* Default selection: the whole monitor, matching niri's initial state. */
	screenshot_ui.start_x = m->m.x;
	screenshot_ui.start_y = m->m.y;
	screenshot_ui.end_x = m->m.x + m->m.width;
	screenshot_ui.end_y = m->m.y + m->m.height;

	/* Bottom-up stacking within LyrOverlay = creation order: freeze frame,
	 * then the dim, then the selection border, then the info readout. */
	screenshotui_freeze(m);

	screenshot_ui.overlay = wlr_scene_rect_create(layers[LyrOverlay],
		m->m.width, m->m.height, (float[]){0, 0, 0, SCREENSHOT_OVERLAY_ALPHA});
	wlr_scene_node_set_position(&screenshot_ui.overlay->node, m->m.x, m->m.y);

	for (i = 0; i < 4; i++)
		screenshot_ui.border[i] = wlr_scene_rect_create(layers[LyrOverlay],
			SCREENSHOT_BORDER_WIDTH, SCREENSHOT_BORDER_WIDTH,
			(float[]){BORDER_COL_R, BORDER_COL_G, BORDER_COL_B, 1.0f});

	wlr_cursor_set_xcursor(cursor, cursor_mgr, "crosshair");

	wlr_log(WLR_INFO, "screenshot UI opened - drag to select, Space/Enter to capture, Escape to cancel");
	status_mark_dirty();
	screenshotui_draw();
}

static void
screenshotui_destroy_scene(void)
{
	int i;

	if (screenshot_ui.overlay)
		wlr_scene_node_destroy(&screenshot_ui.overlay->node);
	screenshot_ui.overlay = NULL;

	for (i = 0; i < 4; i++) {
		if (screenshot_ui.border[i])
			wlr_scene_node_destroy(&screenshot_ui.border[i]->node);
		screenshot_ui.border[i] = NULL;
	}

	screenshotui_info_destroy();

	if (screenshot_ui.frozen_node)
		wlr_scene_node_destroy(&screenshot_ui.frozen_node->node);
	screenshot_ui.frozen_node = NULL;
	if (screenshot_ui.frozen_buf)
		wlr_buffer_drop(screenshot_ui.frozen_buf);
	screenshot_ui.frozen_buf = NULL;
	screenshot_ui.frozen_data = NULL; /* owned by frozen_buf, not freed here */
}

void
screenshotui_cancel(const Arg *arg)
{
	(void)arg;
	if (!screenshot_ui.active)
		return;

	screenshotui_destroy_scene();
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	screenshot_ui.active = false;
	screenshot_ui.mon = NULL;
	screenshot_ui.dragging = false;

	wlr_log(WLR_INFO, "screenshot UI cancelled");
	status_mark_dirty();
}

void
screenshotui_confirm(bool write_to_disk)
{
	Monitor *m;
	int sx, sy, ex, ey;

	if (!screenshot_ui.active)
		return;

	m = screenshot_ui.mon;
	sx = (int)MIN(screenshot_ui.start_x, screenshot_ui.end_x);
	sy = (int)MIN(screenshot_ui.start_y, screenshot_ui.end_y);
	ex = (int)MAX(screenshot_ui.start_x, screenshot_ui.end_x);
	ey = (int)MAX(screenshot_ui.start_y, screenshot_ui.end_y);

	/* write_to_disk always implies clipboard too (niri: Space/Enter does
	 * both; Ctrl+C is clipboard-only). Export from the freeze-frame pixels
	 * (what the user actually confirmed) while frozen_buf still holds them;
	 * fall back to a fresh render only if freezing failed at open. */
	if (m && screenshot_ui.frozen_data)
		capture_export_pixels(screenshot_ui.frozen_data,
				screenshot_ui.frozen_w, screenshot_ui.frozen_h,
				screenshot_ui.frozen_stride,
				m, sx, sy, ex - sx, ey - sy, write_to_disk, true);
	else if (m)
		capture_export_selection(m, sx, sy, ex - sx, ey - sy, write_to_disk, true);

	screenshotui_destroy_scene();
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	screenshot_ui.active = false;
	screenshot_ui.mon = NULL;
	screenshot_ui.dragging = false;

	status_mark_dirty();
}

void
screenshotui_toggle_pointer(void)
{
	if (!screenshot_ui.active)
		return;
	screenshot_ui.show_pointer = !screenshot_ui.show_pointer;
	wlr_log(WLR_INFO, "screenshot UI: pointer %s in capture",
		screenshot_ui.show_pointer ? "shown" : "hidden");
}

void
screenshotui_draw(void)
{
	int x, y, w, h;

	if (!screenshot_ui.active)
		return;
	if (!screenshot_ui.border[0] || !screenshot_ui.border[1]
			|| !screenshot_ui.border[2] || !screenshot_ui.border[3])
		return;

	x = (int)MIN(screenshot_ui.start_x, screenshot_ui.end_x);
	y = (int)MIN(screenshot_ui.start_y, screenshot_ui.end_y);
	w = (int)fabs(screenshot_ui.end_x - screenshot_ui.start_x);
	h = (int)fabs(screenshot_ui.end_y - screenshot_ui.start_y);
	if (w < 1) w = 1;
	if (h < 1) h = 1;

	/* Top */
	wlr_scene_rect_set_size(screenshot_ui.border[0], w, SCREENSHOT_BORDER_WIDTH);
	wlr_scene_node_set_position(&screenshot_ui.border[0]->node, x, y);
	/* Bottom */
	wlr_scene_rect_set_size(screenshot_ui.border[1], w, SCREENSHOT_BORDER_WIDTH);
	wlr_scene_node_set_position(&screenshot_ui.border[1]->node, x, y + h - SCREENSHOT_BORDER_WIDTH);
	/* Left */
	wlr_scene_rect_set_size(screenshot_ui.border[2], SCREENSHOT_BORDER_WIDTH, h);
	wlr_scene_node_set_position(&screenshot_ui.border[2]->node, x, y);
	/* Right */
	wlr_scene_rect_set_size(screenshot_ui.border[3], SCREENSHOT_BORDER_WIDTH, h);
	wlr_scene_node_set_position(&screenshot_ui.border[3]->node, x + w - SCREENSHOT_BORDER_WIDTH, y);

	screenshotui_info_update(x, y, w, h);
}
