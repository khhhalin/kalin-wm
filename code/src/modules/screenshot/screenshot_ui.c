/* Niri-style interactive screenshot UI: Super+Shift+S opens a dim overlay
 * pre-selecting the whole focused monitor; dragging draws a custom region.
 * Keys match niri's screenshot UI exactly: Escape cancels, Space/Enter
 * confirms (disk + clipboard), Ctrl+C confirms clipboard-only, P toggles
 * pointer visibility. Confirm hands the selection box to capture.c's
 * capture_export_selection() for the actual pixel work.
 *
 * Visual style mirrors crop_mode.c's overlay (dark rect + bright border in
 * layers[LyrOverlay]), but with a single selection box instead of a
 * per-client crop target — see crop_mode.c for the sibling implementation.
 *
 * Separately-compiled TU: reads/writes the shared `screenshot_ui` state and
 * links against dwl.c's externed globals (selmon, layers, cursor, cursor_mgr)
 * via kalin.h. */
#include "kalin.h"

#define SCREENSHOT_OVERLAY_ALPHA 0.35f
#define SCREENSHOT_BORDER_BRIGHT 1.0f
#define SCREENSHOT_BORDER_WIDTH 2

void
screenshotui_begin(const Arg *arg)
{
	Monitor *m;
	(void)arg;

	if (!selmon || screenshot_ui.active)
		return;

	m = selmon;
	screenshot_ui.active = true;
	screenshot_ui.mon = m;
	screenshot_ui.dragging = false;
	screenshot_ui.show_pointer = false;

	/* Default selection: the whole monitor, matching niri's initial state. */
	screenshot_ui.start_x = m->m.x;
	screenshot_ui.start_y = m->m.y;
	screenshot_ui.end_x = m->m.x + m->m.width;
	screenshot_ui.end_y = m->m.y + m->m.height;

	screenshot_ui.overlay = wlr_scene_rect_create(layers[LyrOverlay],
		m->m.width, m->m.height, (float[]){0, 0, 0, SCREENSHOT_OVERLAY_ALPHA});
	wlr_scene_node_set_position(&screenshot_ui.overlay->node, m->m.x, m->m.y);

	screenshot_ui.border[0] = wlr_scene_rect_create(layers[LyrOverlay], 0, SCREENSHOT_BORDER_WIDTH,
		(float[]){SCREENSHOT_BORDER_BRIGHT, SCREENSHOT_BORDER_BRIGHT, SCREENSHOT_BORDER_BRIGHT, 1.0f});
	screenshot_ui.border[1] = wlr_scene_rect_create(layers[LyrOverlay], 0, SCREENSHOT_BORDER_WIDTH,
		(float[]){SCREENSHOT_BORDER_BRIGHT, SCREENSHOT_BORDER_BRIGHT, SCREENSHOT_BORDER_BRIGHT, 1.0f});
	screenshot_ui.border[2] = wlr_scene_rect_create(layers[LyrOverlay], SCREENSHOT_BORDER_WIDTH, 0,
		(float[]){SCREENSHOT_BORDER_BRIGHT, SCREENSHOT_BORDER_BRIGHT, SCREENSHOT_BORDER_BRIGHT, 1.0f});
	screenshot_ui.border[3] = wlr_scene_rect_create(layers[LyrOverlay], SCREENSHOT_BORDER_WIDTH, 0,
		(float[]){SCREENSHOT_BORDER_BRIGHT, SCREENSHOT_BORDER_BRIGHT, SCREENSHOT_BORDER_BRIGHT, 1.0f});

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
	 * both; Ctrl+C is clipboard-only). */
	if (m)
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
}
