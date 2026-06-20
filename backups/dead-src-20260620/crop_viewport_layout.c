/* DEPRECATED (read-only reference): this mixed file has been replaced by
 * the runtime modules in src/modules/.
 * MOVED: see modules/crop/crop_mode.c and modules/layout/layout_world.c.
 *
 * Active include order now lives in dwl.c:
 *   crop_mode.c -> layout_world.c -> wallpaper.c -> viewport_ops.c
 */

static void
crop_adjust_column_after_height_change(Client *changed, int delta_h)
{
	Client *c;

	if (!changed || !changed->mon || delta_h == 0)
		return;
	if (changed->isfloating || changed->isfullscreen || changed->layout_state.is_anchored)
		return;

	wl_list_for_each(c, &clients, link) {
		if (c == changed)
			continue;
		if (!VISIBLEON(c, changed->mon) || c->isfloating || c->isfullscreen)
			continue;
		if (c->layout_state.is_anchored)
			continue;
		if (c->layout_state.column != changed->layout_state.column)
			continue;
		if (c->world.y > changed->world.y)
			c->world.y += delta_h;
	}
}

void
cropbegin(const Arg *arg)
{
	Client *c;
	Monitor *m;
	struct wlr_box resetgeo;
	int old_h, delta_h;
	float flash_color[4] = {1.0f, 1.0f, 1.0f, 0.3f};  /* White flash */
	static struct wlr_scene_rect *flash_rect = NULL;
	
	if (!selmon) return;
	c = focustop(selmon);
	if (!c || crop_editor.active) return;
	
	crop_editor.active = true;
	crop_editor.target = c;
	crop_editor.dragging = false;

	/* Entering crop mode resets this window to uncropped/base size first. */
	if (c->crop.saved_base && c->crop.base_w > 0 && c->crop.base_h > 0) {
		old_h = c->geom.height;
		resetgeo.x = c->geom.x;
		resetgeo.y = c->geom.y;
		resetgeo.width = c->crop.base_w;
		resetgeo.height = c->crop.base_h;
		resize(c, resetgeo, 0);
		delta_h = resetgeo.height - old_h;
		crop_adjust_column_after_height_change(c, delta_h);
		c->crop.active = false;
		c->crop.x = 0.0f;
		c->crop.y = 0.0f;
		c->crop.w = 1.0f;
		c->crop.h = 1.0f;
		if (c->mon)
			arrange(c->mon);
	}
	
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
	printstatus();
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
	printstatus();
}

void
cropend(const Arg *arg)
{
	Client *c;
	int sx, sy, ex, ey, w, h;
	int wx, wy, ww, wh;
	int screen_wx, screen_wy;
	int delta_h;
	float cx, cy, cw, ch;
	struct wlr_box newgeo;
	
	if (!crop_editor.active || !crop_editor.dragging) {
		cropcancel(arg);
		return;
	}
	
	c = crop_editor.target;
	if (c) {
		/* Calculate normalized crop values */
		sx = MIN(crop_editor.start_x, crop_editor.end_x);
		sy = MIN(crop_editor.start_y, crop_editor.end_y);
		ex = MAX(crop_editor.start_x, crop_editor.end_x);
		ey = MAX(crop_editor.start_y, crop_editor.end_y);
		w = ex - sx;
		h = ey - sy;
		
		/* Get window geometry */
		wx = c->geom.x;
		wy = c->geom.y;
		ww = c->geom.width;
		wh = c->geom.height;
		screen_wx = (int)(wx - viewport.x);
		screen_wy = (int)(wy - viewport.y);
		
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
		
		/* Calculate crop relative to window */
		cx = (float)(sx - screen_wx) / ww;
		cy = (float)(sy - screen_wy) / wh;
		cw = (float)w / ww;
		ch = (float)h / wh;
		
		/* Clamp to valid range */
		cx = MAX(0, MIN(1, cx));
		cy = MAX(0, MIN(1, cy));
		cw = MAX(0.1, MIN(1 - cx, cw));
		ch = MAX(0.1, MIN(1 - cy, ch));
		
		/* Apply crop */
		c->crop.active = true;
		c->crop.x = cx;
		c->crop.y = cy;
		c->crop.w = cw;
		c->crop.h = ch;
		
		/* Resize window to crop size */
		newgeo.x = wx;
		newgeo.y = wy;
		newgeo.width = (int)(ww * cw);
		newgeo.height = (int)(wh * ch);
		resize(c, newgeo, 0);
		delta_h = newgeo.height - wh;
		crop_adjust_column_after_height_change(c, delta_h);
		if (c->mon)
			arrange(c->mon);
		
		wlr_log(WLR_INFO, "Crop applied: window '%s' resized to %dx%d", 
			client_get_title(c), newgeo.width, newgeo.height);
	}
	
	cropcancel(arg);
}

void
cropdraw(void)
{
	if (!crop_editor.active || !crop_editor.dragging) return;
	if (!crop_editor.border[0] || !crop_editor.border[1]
			|| !crop_editor.border[2] || !crop_editor.border[3])
		return;
	if (!crop_editor.handles[0] || !crop_editor.handles[1]
			|| !crop_editor.handles[2] || !crop_editor.handles[3])
		return;
	if (!crop_editor.crosshair_h || !crop_editor.crosshair_v)
		return;
	
	int x = MIN(crop_editor.start_x, crop_editor.end_x);
	int y = MIN(crop_editor.start_y, crop_editor.end_y);
	int w = (int)fabs(crop_editor.end_x - crop_editor.start_x);
	int h = (int)fabs(crop_editor.end_y - crop_editor.start_y);
	
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
	int half_handle = CROP_HANDLE_SIZE / 2;
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
	int cx = x + w / 2;
	int cy = y + h / 2;
	wlr_scene_rect_set_size(crop_editor.crosshair_h, w / 4, 1);
	wlr_scene_rect_set_size(crop_editor.crosshair_v, 1, h / 4);
	wlr_scene_node_set_position(&crop_editor.crosshair_h->node, cx - w / 8, cy);
	wlr_scene_node_set_position(&crop_editor.crosshair_v->node, cx, cy - h / 8);
	wlr_scene_node_set_enabled(&crop_editor.crosshair_h->node, true);
	wlr_scene_node_set_enabled(&crop_editor.crosshair_v->node, true);
}

/* infinite layout is now in src/layouts/infinite.c */

/* ===== INFINITE LAYOUT (like Niri but 2D) ===== */

/* Coordinate transform macros - must be before use */
/* 
 * World -> Screen: Just subtract viewport position (camera).
 * ZOOM DOES NOT AFFECT POSITIONS - it only scales the buffer content.
 * This gives true "camera zoom" behavior where zooming out shows more
 * of the canvas without moving windows relative to each other.
 */
#define WORLD_TO_SCREEN_X(wx) ((int)((wx) - viewport.x))
#define WORLD_TO_SCREEN_Y(wy) ((int)((wy) - viewport.y))

static float
infinite_topmost_y(Monitor *m)
{
	Client *c;
	float min_y = 0;
	int n = 0;
	
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen && c->world.set) {
			if (!n || c->world.y < min_y)
				min_y = c->world.y;
			n++;
		}
	}
	return n > 0 ? min_y : 0;
}

/* Configuration for column-based layout */

/* ===== HYBRID WINDOW ANCHORING SYSTEM ===== */

/* Niri-style layout: windows stacked vertically in columns */

#define COLUMN_WIDTH 800
#define COLUMN_GAP 20
#define WINDOW_GAP 20

static int
same_column_x(float a, float b)
{
	return fabsf(a - b) < COLUMN_WIDTH / 2.0f;
}

/* Find column layout info */
typedef struct {
	float x;           /* Column x position */
	float bottom;      /* Bottom edge of lowest window */
	int window_count;  /* Number of windows in column */
} ColumnInfo;

/* Get info about the rightmost column */
static ColumnInfo
get_rightmost_column(Monitor *m)
{
	Client *c;
	ColumnInfo col = {0, 0, 0};
	float max_x = 0;
	
	/* Find rightmost column x */
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen && 
		    c->world.set && !c->layout_state.is_anchored) {
			if (c->world.x >= max_x) {
				max_x = c->world.x;
			}
		}
	}
	
	col.x = max_x;
	
	/* Find bottom of windows in this column */
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen && 
		    c->world.set && !c->layout_state.is_anchored) {
			/* Window is in this column if close to column x */
			if (fabs(c->world.x - col.x) < COLUMN_WIDTH / 2) {
				float bottom = c->world.y + c->geom.height;
				if (bottom > col.bottom)
					col.bottom = bottom;
				col.window_count++;
			}
		}
	}
	
	return col;
}

/* Place a window in niri-style layout */
static void
place_window_column(Client *c, Monitor *m)
{
	ColumnInfo col;
	
	if (c->world.set)
		return;
	
	col = get_rightmost_column(m);
	
	/* Niri-like behavior: new toplevels open in a NEW column to the right. */
	if (col.window_count == 0 && col.x == 0) {
		/* First tiled window */
		c->world.x = 0;
		c->world.y = 0;
	} else {
		/* Always create a new column instead of vertical stacking by default */
		c->world.x = col.x + COLUMN_WIDTH + COLUMN_GAP;
		c->world.y = 0;
	}
	
	c->world.set = true;
	c->layout_state.is_anchored = 0;
	c->layout_state.column = (int)(c->world.x / (COLUMN_WIDTH + COLUMN_GAP));
	
	wlr_log(WLR_DEBUG, "Niri-layout: placed window at (%.0f, %.0f) in column %d", 
		c->world.x, c->world.y, c->layout_state.column);
}

/* Anchor the focused window - detaches from column flow, keeps current world position */
static void
client_anchor(const Arg *arg)
{
	Client *c;
	(void)arg;
	c = focustop(selmon);
	if (!c || c->isfloating || c->isfullscreen)
		return;
	
	/* Mark as anchored */
	c->layout_state.is_anchored = 1;
	c->layout_state.column = -1;  /* Not in any column */
	
	/* Window keeps its current world position */
	wlr_log(WLR_DEBUG, "Anchored window at world (%.0f, %.0f)", c->world.x, c->world.y);
	
	/* Recalculate layout for remaining column windows */
	if (selmon)
		arrange(selmon);
}

/* Re-columnize the focused window - returns to column flow */
static void
client_recolumnize(const Arg *arg)
{
	Client *c;
	(void)arg;
	c = focustop(selmon);
	if (!c || c->isfloating || c->isfullscreen)
		return;
	
	/* Mark as not anchored */
	c->layout_state.is_anchored = 0;
	
	/* Place in the column strip */
	c->world.set = false;  /* Reset so place_window_column will position it */
	place_window_column(c, selmon);
	
	wlr_log(WLR_DEBUG, "Re-columnized window at world (%.0f, %.0f)", c->world.x, c->world.y);
	
	/* Recalculate layout */
	if (selmon)
		arrange(selmon);
}

/* Move focused window in 2D world space (anchors tiled windows on first move). */
static void
move_anchored_window(const Arg *arg)
{
	Client *c;
	float step, zoom;

	if (!selmon)
		return;

	c = focustop(selmon);
	if (!c || c->isfloating || c->isfullscreen)
		return;

	/* Promote to anchored mode when user starts moving in freeform space. */
	if (!c->layout_state.is_anchored) {
		c->layout_state.is_anchored = 1;
		c->layout_state.column = -1;
	}

	zoom = viewport.zoom > 0.0f ? viewport.zoom : 1.0f;
	step = 80.0f / zoom;

	switch (arg->i) {
	case DIR_LEFT:
		c->world.x -= step;
		break;
	case DIR_RIGHT:
		c->world.x += step;
		break;
	case DIR_UP:
		c->world.y -= step;
		break;
	case DIR_DOWN:
		c->world.y += step;
		break;
	default:
		return;
	}

	c->world.set = true;
	arrange(selmon);
}

/* Arrange windows: niri-style vertical stacking in columns */
static void
arrange_columns(Monitor *m)
{
	Client *c;
	Client *best;
	struct wlr_box geo;
	float col_x[256];
	float col_width[256];
	float col_next_y[256];
	float col_new_x[256];
	Client *placed[1024];
	int col_count;
	int placed_count;
	int i, j;
	int already_placed;
	float tmp;
	float next_x;

	col_count = 0;
	placed_count = 0;
	
	/* Pass 1: ensure valid client geometry and collect columns with max width. */
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		if (c->layout_state.is_anchored)
			continue;
		
		if (!c->world.set) {
			/* New window - place in niri layout */
			place_window_column(c, m);
			/* Preserve restored/saved full size if available, otherwise use defaults. */
			if (c->crop.saved_base && c->crop.base_w > 0 && c->crop.base_h > 0) {
				c->geom.width = c->crop.base_w;
				c->geom.height = c->crop.base_h;
			} else {
				c->geom.width = COLUMN_WIDTH;
				c->geom.height = (int)(m->w.height * 0.6f);
			}
		}

		if (c->geom.width <= 0)
			c->geom.width = COLUMN_WIDTH;
		if (c->geom.height <= 0)
			c->geom.height = (int)(m->w.height * 0.6f);

		c->layout_state.column = (int)(c->world.x / (COLUMN_WIDTH + COLUMN_GAP));

		for (i = 0; i < col_count; i++) {
			if (same_column_x(c->world.x, col_x[i]))
				break;
		}
		if (i == col_count && col_count < (int)LENGTH(col_x)) {
			col_x[col_count] = c->world.x;
			col_width[col_count] = 0;
			col_next_y[col_count] = 0;
			col_new_x[col_count] = 0;
			col_count++;
		}
		if (i < col_count && c->geom.width > col_width[i])
			col_width[i] = c->geom.width;
	}

	/* Sort columns left-to-right by existing world x to preserve ordering. */
	for (i = 0; i < col_count; i++) {
		for (j = i + 1; j < col_count; j++) {
			if (col_x[j] < col_x[i]) {
				tmp = col_x[i];
				col_x[i] = col_x[j];
				col_x[j] = tmp;

				tmp = col_width[i];
				col_width[i] = col_width[j];
				col_width[j] = tmp;
			}
		}
	}

	/* Compute compact x positions using current column widths. */
	next_x = 0;
	for (i = 0; i < col_count; i++) {
		if (col_width[i] <= 0)
			col_width[i] = COLUMN_WIDTH;
		col_new_x[i] = next_x;
		col_next_y[i] = 0;
		next_x += col_width[i] + COLUMN_GAP;
	}

	/* Pass 2: reflow each column by current world y order (prevents swapping). */
	for (i = 0; i < col_count; i++) {
		for (;;) {
			best = NULL;
			wl_list_for_each(c, &clients, link) {
				if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
					continue;
				if (c->layout_state.is_anchored)
					continue;
				if (!same_column_x(c->world.x, col_x[i]))
					continue;

				already_placed = 0;
				for (int j = 0; j < placed_count; j++) {
					if (placed[j] == c) {
						already_placed = 1;
						break;
					}
				}
				if (already_placed)
					continue;

				if (!best || c->world.y < best->world.y)
					best = c;
			}

			if (!best)
				break;

			best->world.x = col_new_x[i];
			best->world.y = col_next_y[i];
			best->layout_state.column = i;
			col_next_y[i] += best->geom.height + WINDOW_GAP;
			if (placed_count < (int)LENGTH(placed))
				placed[placed_count++] = best;
		}
	}

	/* Pass 3: apply geometry to scene. */
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		if (c->layout_state.is_anchored)
			continue;
		
		/* Position the window at its world coordinates */
		geo.x = (int)c->world.x;
		geo.y = (int)c->world.y;
		geo.width = c->geom.width;
		geo.height = c->geom.height;
		
		/* Validate geometry before resize */
		if (geo.width <= 0 || geo.height <= 0) {
			wlr_log(WLR_ERROR, "arrange_columns: INVALID GEOMETRY");
			continue;
		}
		
		resize(c, geo, 0);
	}
}

void
infinite(Monitor *m)
{
	Client *c;
	Client *new_client = NULL;

	/* Track a newly mapped tiled client before arrangement so follow_new_windows
	 * can center on the actual new window only. */
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen &&
		    !c->layout_state.is_anchored && !c->world.set) {
			new_client = c;
			break;
		}
	}
	
	/* Use the new column arrangement system */
	arrange_columns(m);
	
	/* Handle auto-pan for newly spawned windows only */
	if (viewport.follow_new_windows && new_client && new_client->world.set) {
		float z = viewport.zoom > 0.0f ? viewport.zoom : 1.0f;
		float win_center_x = new_client->world.x + new_client->geom.width / 2.0f;
		float win_center_y = new_client->world.y + new_client->geom.height / 2.0f;
		viewport.target_x = win_center_x - m->w.width / (2.0f * z);
		viewport.target_y = win_center_y - m->w.height / (2.0f * z);
		if (viewport.smooth_pan)
			viewport.animating = 1;
		else {
			viewport.x = viewport.target_x;
			viewport.y = viewport.target_y;
			viewport.animating = 0;
		}
	}
}

/* ===== VIEWPORT AND WALLPAPER FUNCTIONS ===== */

static void
wallpaper_destroy(void)
{
	if (wallpaper.tree) {
		wlr_scene_node_destroy(&wallpaper.tree->node);
		wallpaper.tree = NULL;
	}
	free(wallpaper.tiles);
	wallpaper.tiles = NULL;
	wallpaper.tiles_x = 0;
	wallpaper.tiles_y = 0;
	wallpaper.tile_size = 0;
	wallpaper.configured_w = 0;
	wallpaper.configured_h = 0;
}

static void
wallpaper_build_blue_room_tile(struct wlr_scene_tree *tile, int tile_size)
{
	int wall_h;
	int floor_h;
	int baseboard_h;
	int stripe_w;
	int x;
	struct wlr_scene_rect *r;

	/*
	 * Original “blue room” repeating tile: cool wall panels + warm floor.
	 * This is an original geometric pattern (no asset copying).
	 */
	wall_h = (int)(tile_size * 0.68f);
	floor_h = tile_size - wall_h;
	baseboard_h = 6;
	stripe_w = 10;

	/* Base */
	r = wlr_scene_rect_create(tile, tile_size, tile_size, (float[]){0.06f, 0.07f, 0.10f, 1.0f});
	wlr_scene_node_set_position(&r->node, 0, 0);

	/* Wall */
	r = wlr_scene_rect_create(tile, tile_size, wall_h, (float[]){0.10f, 0.14f, 0.24f, 1.0f});
	wlr_scene_node_set_position(&r->node, 0, 0);

	/* Wall panel stripes */
	for (x = 0; x < tile_size; x += 80) {
		r = wlr_scene_rect_create(tile, stripe_w, wall_h, (float[]){0.12f, 0.17f, 0.29f, 1.0f});
		wlr_scene_node_set_position(&r->node, x + 18, 0);
		r = wlr_scene_rect_create(tile, 2, wall_h, (float[]){0.05f, 0.07f, 0.12f, 0.85f});
		wlr_scene_node_set_position(&r->node, x + 18 + stripe_w + 6, 0);
	}

	/* Baseboard */
	r = wlr_scene_rect_create(tile, tile_size, baseboard_h, (float[]){0.18f, 0.12f, 0.08f, 1.0f});
	wlr_scene_node_set_position(&r->node, 0, wall_h - baseboard_h);

	/* Floor */
	r = wlr_scene_rect_create(tile, tile_size, floor_h, (float[]){0.16f, 0.10f, 0.06f, 1.0f});
	wlr_scene_node_set_position(&r->node, 0, wall_h);

	/* Floor plank lines */
	for (x = 0; x < tile_size; x += 48) {
		r = wlr_scene_rect_create(tile, 2, floor_h, (float[]){0.08f, 0.05f, 0.03f, 0.55f});
		wlr_scene_node_set_position(&r->node, x + 24, wall_h);
	}

	/* Framed “window” on wall */
	r = wlr_scene_rect_create(tile, 170, 120, (float[]){0.72f, 0.80f, 0.92f, 1.0f});
	wlr_scene_node_set_position(&r->node, 96, 72);
	r = wlr_scene_rect_create(tile, 170, 6, (float[]){0.70f, 0.63f, 0.20f, 1.0f});
	wlr_scene_node_set_position(&r->node, 96, 72);
	r = wlr_scene_rect_create(tile, 6, 120, (float[]){0.70f, 0.63f, 0.20f, 1.0f});
	wlr_scene_node_set_position(&r->node, 96, 72);
	r = wlr_scene_rect_create(tile, 170, 6, (float[]){0.70f, 0.63f, 0.20f, 1.0f});
	wlr_scene_node_set_position(&r->node, 96, 72 + 120 - 6);
	r = wlr_scene_rect_create(tile, 6, 120, (float[]){0.70f, 0.63f, 0.20f, 1.0f});
	wlr_scene_node_set_position(&r->node, 96 + 170 - 6, 72);

	/* Rug */
	r = wlr_scene_rect_create(tile, 260, 120, (float[]){0.10f, 0.23f, 0.30f, 0.85f});
	wlr_scene_node_set_position(&r->node, 110, wall_h + (floor_h / 2) - 60);
	r = wlr_scene_rect_create(tile, 260, 4, (float[]){0.06f, 0.12f, 0.16f, 0.85f});
	wlr_scene_node_set_position(&r->node, 110, wall_h + (floor_h / 2) - 60);
	r = wlr_scene_rect_create(tile, 260, 4, (float[]){0.06f, 0.12f, 0.16f, 0.85f});
	wlr_scene_node_set_position(&r->node, 110, wall_h + (floor_h / 2) + 56);

	/* Corner vignette */
	r = wlr_scene_rect_create(tile, tile_size, 24, (float[]){0.0f, 0.0f, 0.0f, 0.18f});
	wlr_scene_node_set_position(&r->node, 0, 0);
	r = wlr_scene_rect_create(tile, 24, tile_size, (float[]){0.0f, 0.0f, 0.0f, 0.18f});
	wlr_scene_node_set_position(&r->node, 0, 0);
}

static void
wallpaper_configure(int w, int h)
{
	int tiles_x;
	int tiles_y;
	int total;
	int i;
	int tile_size;

	if (w <= 0 || h <= 0)
		return;

	/* Configure only when needed */
	if (wallpaper.tree && wallpaper.configured_w == w && wallpaper.configured_h == h)
		return;

	wallpaper_destroy();

	/* Tile size tuned for “room” readability and reasonable node counts */
	tile_size = 640;
	tiles_x = (w / tile_size) + 3;
	tiles_y = (h / tile_size) + 3;
	if (tiles_x < 3) tiles_x = 3;
	if (tiles_y < 3) tiles_y = 3;

	wallpaper.tree = wlr_scene_tree_create(layers[LyrBg]);
	wallpaper.tile_size = tile_size;
	wallpaper.tiles_x = tiles_x;
	wallpaper.tiles_y = tiles_y;
	wallpaper.configured_w = w;
	wallpaper.configured_h = h;

	total = tiles_x * tiles_y;
	wallpaper.tiles = ecalloc(total, sizeof(*wallpaper.tiles));
	for (i = 0; i < total; i++) {
		struct wlr_scene_tree *tile = wlr_scene_tree_create(wallpaper.tree);
		wallpaper.tiles[i] = tile;
		wallpaper_build_blue_room_tile(tile, tile_size);
	}
}

static void
wallpaper_update(void)
{
	int base_x;
	int base_y;
	int x;
	int y;
	int idx;
	int tile_size;
	float cam_x;
	float cam_y;

	if (!wallpaper.tree || !wallpaper.tiles || wallpaper.tiles_x <= 0 || wallpaper.tiles_y <= 0)
		return;
	if (wallpaper.tile_size <= 0)
		return;

	tile_size = wallpaper.tile_size;
	cam_x = viewport.x;
	cam_y = viewport.y;

	/* World-anchored background: apply the same world->screen transform as windows. */
	wlr_scene_node_set_position(&wallpaper.tree->node, (int)(-cam_x), (int)(-cam_y));

	base_x = (int)floorf(cam_x / (float)tile_size) - 1;
	base_y = (int)floorf(cam_y / (float)tile_size) - 1;

	idx = 0;
	for (y = 0; y < wallpaper.tiles_y; y++) {
		for (x = 0; x < wallpaper.tiles_x; x++) {
			int world_x = (base_x + x) * tile_size;
			int world_y = (base_y + y) * tile_size;
			wlr_scene_node_set_position(&wallpaper.tiles[idx]->node, world_x, world_y);
			idx++;
		}
	}
}


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

static void
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
	if (!c || !c->mon)
		return;
	
	m = c->mon;
	
	/* Center the camera so the window appears in the middle of the monitor */
	viewport_move_to(
		c->world.x + c->geom.width / 2.0f - m->w.width / (2.0f * viewport.zoom),
		c->world.y + c->geom.height / 2.0f - m->w.height / (2.0f * viewport.zoom),
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

/* ===== END VIEWPORT AND WALLPAPER ===== */

