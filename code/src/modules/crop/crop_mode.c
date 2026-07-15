/* Crop mode operations: enter, draw, apply/cancel.
 *
 * Separately-compiled TU: owns the shared `crop_editor` UI state, reads the
 * `viewport` camera, and links against dwl.c's externed globals/functions
 * (clients, selmon, cursor, layers, focustop, resize, arrange, printstatus)
 * via kalin.h; client_* accessors come from client_inline.h. */
#include "kalin.h"
#include "client_inline.h"

/* Crop mode visuals - bright border on transparent selection */
#define CROP_OVERLAY_ALPHA 0.5f     /* Dark overlay for contrast */
#define CROP_BORDER_BRIGHT 1.0f     /* Full white brightness */
#define CROP_HANDLE_SIZE 12         /* Corner handle size in pixels */
#define CROP_BORDER_WIDTH 2         /* Border line thickness */

/* Restore a client to its saved uncropped/base size and clear its crop rect.
 * No-op if no base size was captured. */
static void
crop_restore_base(Client *c)
{
	struct wlr_box resetgeo;

	if (!c || !c->crop.saved_base || c->crop.base_w <= 0 || c->crop.base_h <= 0)
		return;

	resetgeo.x = c->geom.x;
	resetgeo.y = c->geom.y;
	resetgeo.width = c->crop.base_w;
	resetgeo.height = c->crop.base_h;
	resize(c, resetgeo, 0);
	c->crop.active = false;
	c->crop.x = 0.0f;
	c->crop.y = 0.0f;
	c->crop.w = 1.0f;
	c->crop.h = 1.0f;
	if (c->mon)
		arrange_mark_dirty(c->mon);
}

void
cropbegin(const Arg *arg)
{
	Client *c;
	Monitor *m;
	float flash_color[4] = {1.0f, 1.0f, 1.0f, 0.3f};  /* White flash */
	static struct wlr_scene_rect *flash_rect = NULL;

	if (!selmon) return;
	c = focustop(selmon);
	if (!c || crop_editor.active) return;

	crop_editor.active = true;
	crop_editor.target = c;
	crop_editor.dragging = false;

	/* Entering crop mode resets this window to uncropped/base size first. */
	crop_restore_base(c);

	m = selmon;
	
	/* Visual feedback: brief white flash on screen to indicate crop mode */
	flash_rect = wlr_scene_rect_create(layers[LyrOverlay], 
		m->m.width, m->m.height, flash_color);
	wlr_scene_node_set_position(&flash_rect->node, m->m.x, m->m.y);
	
	/* Create dark overlay covering entire output */
	crop_editor.overlay = wlr_scene_rect_create(layers[LyrOverlay], 
		m->m.width, m->m.height, 
		(float[]){0, 0, 0, CROP_OVERLAY_ALPHA});
	wlr_scene_node_set_position(&crop_editor.overlay->node, m->m.x, m->m.y);
	
	/* Create border lines - bright white outline, no fill */
	/* Top border */
	crop_editor.border[0] = wlr_scene_rect_create(layers[LyrOverlay], 0, CROP_BORDER_WIDTH,
		(float[]){CROP_BORDER_BRIGHT, CROP_BORDER_BRIGHT, CROP_BORDER_BRIGHT, 1.0f});
	/* Bottom border */
	crop_editor.border[1] = wlr_scene_rect_create(layers[LyrOverlay], 0, CROP_BORDER_WIDTH,
		(float[]){CROP_BORDER_BRIGHT, CROP_BORDER_BRIGHT, CROP_BORDER_BRIGHT, 1.0f});
	/* Left border */
	crop_editor.border[2] = wlr_scene_rect_create(layers[LyrOverlay], CROP_BORDER_WIDTH, 0,
		(float[]){CROP_BORDER_BRIGHT, CROP_BORDER_BRIGHT, CROP_BORDER_BRIGHT, 1.0f});
	/* Right border */
	crop_editor.border[3] = wlr_scene_rect_create(layers[LyrOverlay], CROP_BORDER_WIDTH, 0,
		(float[]){CROP_BORDER_BRIGHT, CROP_BORDER_BRIGHT, CROP_BORDER_BRIGHT, 1.0f});
	
	for (int i = 0; i < 4; i++) {
		wlr_scene_node_set_enabled(&crop_editor.border[i]->node, false);
	}
	
	/* Create corner handles - bright white squares */
	for (int i = 0; i < 4; i++) {
		crop_editor.handles[i] = wlr_scene_rect_create(layers[LyrOverlay], 
			CROP_HANDLE_SIZE, CROP_HANDLE_SIZE,
			(float[]){1.0f, 1.0f, 1.0f, 1.0f});
		wlr_scene_node_set_enabled(&crop_editor.handles[i]->node, false);
	}
	
	/* Create crosshair lines for center point - subtle */
	crop_editor.crosshair_h = wlr_scene_rect_create(layers[LyrOverlay], 0, 1,
		(float[]){1.0f, 1.0f, 1.0f, 0.5f});
	crop_editor.crosshair_v = wlr_scene_rect_create(layers[LyrOverlay], 1, 0,
		(float[]){1.0f, 1.0f, 1.0f, 0.5f});
	wlr_scene_node_set_enabled(&crop_editor.crosshair_h->node, false);
	wlr_scene_node_set_enabled(&crop_editor.crosshair_v->node, false);
	
	/* Set crosshair cursor for precision selection */
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "crosshair");
	
	/* Remove flash after a brief moment - it served its purpose */
	wlr_scene_node_destroy(&flash_rect->node);
	flash_rect = NULL;
	
	wlr_log(WLR_INFO, "Crop mode started - drag to select region, press Super+Shift+R to cancel");
	status_mark_dirty();
}

void
cropcancel(const Arg *arg)
{
	if (!crop_editor.active) return;
	
	if (crop_editor.overlay)
		wlr_scene_node_destroy(&crop_editor.overlay->node);
	crop_editor.overlay = NULL;
	
	/* Destroy border lines */
	for (int i = 0; i < 4; i++) {
		if (crop_editor.border[i])
			wlr_scene_node_destroy(&crop_editor.border[i]->node);
		crop_editor.border[i] = NULL;
	}
	
	/* Destroy corner handles */
	for (int i = 0; i < 4; i++) {
		if (crop_editor.handles[i])
			wlr_scene_node_destroy(&crop_editor.handles[i]->node);
		crop_editor.handles[i] = NULL;
	}
	
	/* Destroy crosshair lines */
	if (crop_editor.crosshair_h)
		wlr_scene_node_destroy(&crop_editor.crosshair_h->node);
	if (crop_editor.crosshair_v)
		wlr_scene_node_destroy(&crop_editor.crosshair_v->node);
	crop_editor.crosshair_h = NULL;
	crop_editor.crosshair_v = NULL;
	
	/* Restore default cursor */
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
	
	crop_editor.active = false;
	crop_editor.target = NULL;
	crop_editor.dragging = false;
	
	wlr_log(WLR_INFO, "Crop mode cancelled");
	status_mark_dirty();
}

/* Reset the crop target to its uncropped/base size and leave crop mode. Bound
 * to an unmodified 'r' while crop mode is active; dispatched from keypress()
 * directly (not a normal bind) so 'r' still types normally outside crop mode. */
void
cropreset(const Arg *arg)
{
	if (!crop_editor.active)
		return;

	crop_restore_base(crop_editor.target);
	wlr_log(WLR_INFO, "Crop reset to original size");
	cropcancel(arg);
}

void
cropend(const Arg *arg)
{
	Client *c;
	int sx, sy, ex, ey, w, h;
	int wx, wy, ww, wh;
	int base_w, base_h;
	int screen_wx, screen_wy;
	float cx, cy, cw, ch;
	float crop_zf;
	struct wlr_box newgeo;
	
	if (!crop_editor.active || !crop_editor.dragging) {
		cropcancel(arg);
		return;
	}
	
	c = crop_editor.target;
	if (c) {
		/* Calculate normalized crop values */
		sx = (int)MIN(crop_editor.start_x, crop_editor.end_x);
		sy = (int)MIN(crop_editor.start_y, crop_editor.end_y);
		ex = (int)MAX(crop_editor.start_x, crop_editor.end_x);
		ey = (int)MAX(crop_editor.start_y, crop_editor.end_y);
		w = ex - sx;
		h = ey - sy;
		
		/* Get window geometry */
		wx = c->geom.x;
		wy = c->geom.y;
		ww = c->geom.width;
		wh = c->geom.height;
		base_w = ww;
		base_h = wh;
		/* The window is displayed at (world - holder camera) * zoom + monitor
		 * offset, sized geom * zoom. The crop selection is in screen pixels,
		 * so map through the same (per-monitor) camera. */
		crop_zf = MON_ZOOM_SAFE(c->mon);
		screen_wx = WORLD_TO_SCREEN_X(c->mon, wx);
		screen_wy = WORLD_TO_SCREEN_Y(c->mon, wy);

		/* Check for division by zero */
		if (ww <= 0 || wh <= 0) {
			cropcancel(arg);
			return;
		}

		/* Capture full/base size on first crop so we can restore it later. */
		if (!c->crop.saved_base) {
			c->crop.base_w = ww;
			c->crop.base_h = wh;
			c->crop.saved_base = true;
		}
		if (c->crop.saved_base && c->crop.base_w > 0 && c->crop.base_h > 0) {
			base_w = c->crop.base_w;
			base_h = c->crop.base_h;
		}
		
		/* Calculate crop relative to window (on-screen size is geom * zoom). */
		cx = (float)(sx - screen_wx) / (ww * crop_zf);
		cy = (float)(sy - screen_wy) / (wh * crop_zf);
		cw = (float)w / (ww * crop_zf);
		ch = (float)h / (wh * crop_zf);
		
		/* Clamp to valid range */
		cx = MAX(0, MIN(1, cx));
		cy = MAX(0, MIN(1, cy));
		cw = MAX(0.1f, MIN(1 - cx, cw));
		ch = MAX(0.1f, MIN(1 - cy, ch));
		
		/* Apply crop */
		c->crop.active = true;
		c->crop.x = cx;
		c->crop.y = cy;
		c->crop.w = cw;
		c->crop.h = ch;
		
		/* Resize window to crop size */
		newgeo.x = wx;
		newgeo.y = wy;
		newgeo.width = (int)lroundf(base_w * cw);
		newgeo.height = (int)lroundf(base_h * ch);
		newgeo.width = MAX(1 + 2 * (int)c->bw, newgeo.width);
		newgeo.height = MAX(1 + 2 * (int)c->bw, newgeo.height);
		resize(c, newgeo, 0);
		if (c->mon)
			arrange_mark_dirty(c->mon);
		
		wlr_log(WLR_INFO, "Crop applied: window '%s' visible region %dx%d (base %dx%d)", 
			client_get_title(c), newgeo.width, newgeo.height, base_w, base_h);
	}
	
	cropcancel(arg);
}

void
cropdraw(void)
{
	int x, y, w, h;
	int half_handle;
	int cx, cy;

	if (!crop_editor.active || !crop_editor.dragging) return;
	if (!crop_editor.border[0] || !crop_editor.border[1]
			|| !crop_editor.border[2] || !crop_editor.border[3])
		return;
	if (!crop_editor.handles[0] || !crop_editor.handles[1]
			|| !crop_editor.handles[2] || !crop_editor.handles[3])
		return;
	if (!crop_editor.crosshair_h || !crop_editor.crosshair_v)
		return;

	x = (int)MIN(crop_editor.start_x, crop_editor.end_x);
	y = (int)MIN(crop_editor.start_y, crop_editor.end_y);
	w = (int)fabs(crop_editor.end_x - crop_editor.start_x);
	h = (int)fabs(crop_editor.end_y - crop_editor.start_y);
	
	/* Ensure minimum size */
	if (w < 10) w = 10;
	if (h < 10) h = 10;
	
	/* Update border lines - bright white outline, transparent fill */
	/* Top border */
	wlr_scene_rect_set_size(crop_editor.border[0], w, CROP_BORDER_WIDTH);
	wlr_scene_node_set_position(&crop_editor.border[0]->node, x, y);
	/* Bottom border */
	wlr_scene_rect_set_size(crop_editor.border[1], w, CROP_BORDER_WIDTH);
	wlr_scene_node_set_position(&crop_editor.border[1]->node, x, y + h - CROP_BORDER_WIDTH);
	/* Left border */
	wlr_scene_rect_set_size(crop_editor.border[2], CROP_BORDER_WIDTH, h);
	wlr_scene_node_set_position(&crop_editor.border[2]->node, x, y);
	/* Right border */
	wlr_scene_rect_set_size(crop_editor.border[3], CROP_BORDER_WIDTH, h);
	wlr_scene_node_set_position(&crop_editor.border[3]->node, x + w - CROP_BORDER_WIDTH, y);
	
	for (int i = 0; i < 4; i++) {
		wlr_scene_node_set_enabled(&crop_editor.border[i]->node, true);
	}
	
	/* Position corner handles */
	half_handle = CROP_HANDLE_SIZE / 2;
	/* Top-left */
	wlr_scene_node_set_position(&crop_editor.handles[0]->node, x - half_handle, y - half_handle);
	/* Top-right */
	wlr_scene_node_set_position(&crop_editor.handles[1]->node, x + w - half_handle, y - half_handle);
	/* Bottom-left */
	wlr_scene_node_set_position(&crop_editor.handles[2]->node, x - half_handle, y + h - half_handle);
	/* Bottom-right */
	wlr_scene_node_set_position(&crop_editor.handles[3]->node, x + w - half_handle, y + h - half_handle);
	
	for (int i = 0; i < 4; i++) {
		wlr_scene_node_set_enabled(&crop_editor.handles[i]->node, true);
	}
	
	/* Update crosshair lines at center - subtle white */
	cx = x + w / 2;
	cy = y + h / 2;
	wlr_scene_rect_set_size(crop_editor.crosshair_h, w / 4, 1);
	wlr_scene_rect_set_size(crop_editor.crosshair_v, 1, h / 4);
	wlr_scene_node_set_position(&crop_editor.crosshair_h->node, cx - w / 8, cy);
	wlr_scene_node_set_position(&crop_editor.crosshair_v->node, cx, cy - h / 8);
	wlr_scene_node_set_enabled(&crop_editor.crosshair_h->node, true);
	wlr_scene_node_set_enabled(&crop_editor.crosshair_v->node, true);
}
