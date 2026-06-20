/* ============================================================================
 * Clean Niri-Style Layout Implementation
 * ============================================================================
 * 
 * Layout paradigm: Scrollable horizontal canvas with vertical column stacking
 * 
 * Column Layout:
 * - Windows arranged in vertical columns
 * - Columns arranged horizontally (scroll left/right)
 * - New windows stack below active window in same column, or create new column
 * 
 * World Coordinates:
 * - Layout happens in world space (infinite canvas)
 * - Rendering transforms world to screen via viewport
 * - Zoom affects buffer scale, not positions
 */

#include "include/kalin.h"

/* Column layout configuration */
#define COL_WIDTH       800     /* Default column width */
#define COL_GAP         50      /* Gap between columns */
#define WIN_GAP         20      /* Gap between windows in a column */
#define COL_MIN_HEIGHT  400     /* Minimum window height */

/* ============================================================================
 * Coordinate Transforms
 * ============================================================================
 */

static int
world_to_screen_x(float wx)
{
	return (int)((wx - viewport.x) * viewport.zoom);
}

static int
world_to_screen_y(float wy)
{
	return (int)((wy - viewport.y) * viewport.zoom);
}

static float
screen_to_world_x(int sx)
{
	return (float)(sx / viewport.zoom + viewport.x);
}

static float
screen_to_world_y(int sy)
{
	return (float)(sy / viewport.zoom + viewport.y);
}

/* ============================================================================
 * Column Management
 * ============================================================================
 */

/* Find which column a window belongs to based on its x position */
static int
window_get_column(Client *c)
{
	if (c->layout_state.is_anchored || !c->world.set)
		return -1;
	return (int)(c->world.x / (COL_WIDTH + COL_GAP));
}

/* Get the rightmost occupied column index */
static int
get_max_column(Monitor *m)
{
	Client *c;
	int max_col = -1;
	
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen && 
		    c->world.set && !c->layout_state.is_anchored) {
			int col = window_get_column(c);
			if (col > max_col)
				max_col = col;
		}
	}
	return max_col;
}

/* Get bottom of a column (y position to place next window) */
static float
get_column_bottom(Monitor *m, int col_idx)
{
	Client *c;
	float max_bottom = 0;
	float col_x = col_idx * (COL_WIDTH + COL_GAP);
	int found = 0;
	
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen && 
		    c->world.set && !c->layout_state.is_anchored) {
			/* Check if window is in this column (within tolerance) */
			if (fabs(c->world.x - col_x) < COL_WIDTH) {
				float bottom = c->world.y + c->geom.height;
				if (bottom > max_bottom)
					max_bottom = bottom;
				found = 1;
			}
		}
	}
	
	return found ? max_bottom + WIN_GAP : 0;
}

/* Count windows in a column */
static int
count_column_windows(Monitor *m, int col_idx)
{
	Client *c;
	int count = 0;
	float col_x = col_idx * (COL_WIDTH + COL_GAP);
	
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen && 
		    c->world.set && !c->layout_state.is_anchored) {
			if (fabs(c->world.x - col_x) < COL_WIDTH)
				count++;
		}
	}
	return count;
}

/* ============================================================================
 * Window Placement
 * ============================================================================
 */

/* Place a new window in the layout */
void
place_window_column(Client *c, Monitor *m)
{
	int target_col;
	float col_x, col_y;
	
	if (c->world.set)
		return;
	
	/* Determine target column:
	 * - If focused window is in a column, use that column
	 * - Otherwise, create new column to the right
	 */
	Client *focused = focustop(m);
	if (focused && focused != c && !focused->isfloating && !focused->isfullscreen &&
	    focused->world.set && !focused->layout_state.is_anchored) {
		/* Stack in same column as focused window */
		target_col = window_get_column(focused);
	} else {
		/* Create new column to the right */
		target_col = get_max_column(m) + 1;
	}
	
	if (target_col < 0)
		target_col = 0;
	
	col_x = target_col * (COL_WIDTH + COL_GAP);
	col_y = get_column_bottom(m, target_col);
	
	c->world.x = col_x;
	c->world.y = col_y;
	c->world.set = true;
	c->layout_state.is_anchored = 0;
	c->layout_state.column = target_col;
	
	/* Set default size if not already set */
	if (c->geom.width <= 0)
		c->geom.width = COL_WIDTH;
	if (c->geom.height <= 0)
		c->geom.height = MAX(COL_MIN_HEIGHT, (int)(m->w.height * 0.6f));
	
	wlr_log(WLR_DEBUG, "Placed window %p at col %d pos (%.0f, %.0f)",
		(void*)c, target_col, col_x, col_y);
}

/* ============================================================================
 * Layout Arrangement
 * ============================================================================
 */

/* Arrange all column windows */
void
arrange_columns(Monitor *m)
{
	Client *c;
	struct wlr_box geo;
	
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		
		/* Place new windows */
		if (!c->world.set) {
			place_window_column(c, m);
		}
		
		/* Anchored windows keep their world position */
		if (c->layout_state.is_anchored) {
			geo.x = (int)c->world.x;
			geo.y = (int)c->world.y;
			geo.width = c->geom.width;
			geo.height = c->geom.height;
			resize(c, geo, 0);
			continue;
		}
		
		/* Column windows - ensure valid dimensions */
		if (c->geom.width <= 0)
			c->geom.width = COL_WIDTH;
		if (c->geom.height <= 0)
			c->geom.height = COL_MIN_HEIGHT;
		
		geo.x = (int)c->world.x;
		geo.y = (int)c->world.y;
		geo.width = c->geom.width;
		geo.height = c->geom.height;
		
		resize(c, geo, 0);
	}
}

/* Main layout entry point */
void
infinite(Monitor *m)
{
	arrange_columns(m);
}

/* ============================================================================
 * Anchoring System
 * ============================================================================
 */

/* Anchor (detach) focused window from column flow */
void
client_anchor(const Arg *arg)
{
	Client *c = focustop(selmon);
	
	if (!c || c->isfloating || c->isfullscreen)
		return;
	
	c->layout_state.is_anchored = 1;
	c->layout_state.column = -1;
	
	wlr_log(WLR_DEBUG, "Anchored window %p at (%.0f, %.0f)",
		(void*)c, c->world.x, c->world.y);
}

/* Return anchored window to column flow */
void
client_recolumnize(const Arg *arg)
{
	Client *c = focustop(selmon);
	
	if (!c || c->isfloating || c->isfullscreen)
		return;
	
	c->layout_state.is_anchored = 0;
	c->world.set = false;  /* Force re-placement */
	place_window_column(c, selmon);
	
	if (selmon)
		arrange(selmon);
}

/* ============================================================================
 * Viewport Functions
 * ============================================================================
 */

void
viewport_pan(const Arg *arg)
{
	float *d = (float *)arg->v;
	viewport.x += d[0];
	viewport.y += d[1];
}

void
viewport_zoom(const Arg *arg)
{
	float new_zoom = viewport.zoom + arg->f;
	if (new_zoom < 0.1f) new_zoom = 0.1f;
	if (new_zoom > 5.0f) new_zoom = 5.0f;
	viewport.zoom = new_zoom;
}

void
viewport_reset(const Arg *arg)
{
	viewport.x = 0;
	viewport.y = 0;
	viewport.zoom = 1.0f;
}

void
viewport_center_on(Client *c)
{
	Monitor *m;
	if (!c || !c->mon)
		return;
	m = c->mon;
	
	viewport.x = c->world.x + c->geom.width / 2.0f - m->w.width / (2.0f * viewport.zoom);
	viewport.y = c->world.y + c->geom.height / 2.0f - m->w.height / (2.0f * viewport.zoom);
}

void
viewport_toggle_follow(const Arg *arg)
{
	viewport.follow = !viewport.follow;
}

void
viewport_toggle_follow_new(const Arg *arg)
{
	viewport.follow_new_windows = !viewport.follow_new_windows;
}

void
viewport_follow_focus(void)
{
	Client *c;
	if (!viewport.follow)
		return;
	c = focustop(selmon);
	if (c && !c->isfloating && !c->isfullscreen)
		viewport_center_on(c);
}
