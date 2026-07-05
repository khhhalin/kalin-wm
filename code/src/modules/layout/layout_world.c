/* Infinite world layout and column strip window operations.
 *
 * Separately-compiled TU: reads the shared `viewport` camera and links against
 * dwl.c's externed clients/selmon/focustop/arrange/resize via kalin.h. */
#include "kalin.h"

/* infinite layout is now in src/layouts/infinite.c */

/* ===== INFINITE LAYOUT (like Niri but 2D) ===== */

/* Coordinate transform macros (WORLD_TO_SCREEN / SCREEN_TO_WORLD) are defined
 * early in dwl.c so input/crop/layout all share the same zoom-aware transform. */


/* Configuration for column-based layout */

/* Niri-style layout: windows stacked vertically in columns */

#define COLUMN_WIDTH 800
#define COLUMN_GAP 20
#define WINDOW_GAP 20

int
same_column_x(float a, float b)
{
	/* Columns are identified by near-identical x origins. A wide tolerance
	 * causes adjacent narrow columns to collapse into one and overlap. */
	return fabsf(a - b) < 2.0f;
}

/* ===== Adaptive spawn cursor =====
 * Remembers where the next new window should land: to the right of the focused
 * window by default, or stacked on top of a column after a vertical move. */
typedef enum { SPAWN_RIGHT_OF_FOCUS, SPAWN_STACK_TOP } SpawnMode;
static struct { Client *anchor; SpawnMode mode; } spawn_cursor = { NULL, SPAWN_RIGHT_OF_FOCUS };

/* Is `target` still a live tiled client? (anchor may have been destroyed.) */
static int
client_is_live(Client *target)
{
	Client *c;
	if (!target)
		return 0;
	wl_list_for_each(c, &clients, link)
		if (c == target)
			return 1;
	return 0;
}

/* Topmost (smallest) world.y among windows in the column at col_x. */
static float
column_min_y(Monitor *m, float col_x, Client *exclude)
{
	Client *c;
	float miny = 0;
	int found = 0;
	wl_list_for_each(c, &clients, link) {
		if (c == exclude || !VISIBLEON(c, m) || c->isfloating || c->isfullscreen || !c->world.set)
			continue;
		if (!same_column_x(c->world.x, col_x))
			continue;
		if (!found || c->world.y < miny) {
			miny = c->world.y;
			found = 1;
		}
	}
	return found ? miny : 0;
}

/* After a vertical move, the next spawn should stack on top of c's column. */
static void
spawn_cursor_set_stack(Client *c)
{
	spawn_cursor.anchor = c;
	spawn_cursor.mode = SPAWN_STACK_TOP;
}

/* Find column layout info */
typedef struct {
	float x;           /* Column x position */
	float right;       /* Right edge of widest window in column */
	float bottom;      /* Bottom edge of lowest window */
	int window_count;  /* Number of windows in column */
} ColumnInfo;

static ColumnInfo
get_rightmost_column(Monitor *m)
{
	Client *c;
	ColumnInfo col = {0, 0, 0, 0};
	float max_x = 0;
	
	/* Find rightmost column x */
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen && c->world.set) {
			if (c->world.x >= max_x) {
				max_x = c->world.x;
			}
		}
	}
	
	col.x = max_x;
	col.right = max_x;
	
	/* Find bottom of windows in this column */
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen && c->world.set) {
			if (same_column_x(c->world.x, col.x)) {
				float bottom = c->world.y + c->geom.height;
				float right = c->world.x + c->geom.width;
				if (bottom > col.bottom)
					col.bottom = bottom;
				if (right > col.right)
					col.right = right;
				col.window_count++;
			}
		}
	}
	
	return col;
}

/* Snap target for a window dropped at drop_x (world units) after a Win+drag:
 * if the drop overlaps an existing column, return that column's x (join its
 * stack); otherwise return drop_x so arrange_columns orders it into a new
 * column at the drop position. `exclude` is the dragged window itself. */
float
nearest_column_x(Monitor *m, Client *exclude, float drop_x)
{
	Client *c;
	float best_x = drop_x;
	float best_dist = 1e9f;

	wl_list_for_each(c, &clients, link) {
		float left, right, d;
		if (c == exclude)
			continue;
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen || !c->world.set)
			continue;
		left = c->world.x;
		right = c->world.x + c->geom.width;
		if (drop_x < left)
			d = left - drop_x;
		else if (drop_x > right)
			d = drop_x - right;
		else
			d = 0.0f; /* dropped inside this column */
		if (d < best_dist) {
			best_dist = d;
			best_x = c->world.x;
		}
	}

	/* Join only when the drop actually lands on a column; otherwise keep the raw
	 * drop x so it becomes a new column ordered by position. */
	return best_dist == 0.0f ? best_x : drop_x;
}

/* Place a window in niri-style layout */
void
place_window_column(Client *c, Monitor *m)
{
	ColumnInfo col;
	Client *f = NULL, *w;

	if (!m || m->w.width <= 0 || m->w.height <= 0)
		return;

	if (c->world.set)
		return;

	/* Adaptive: stack on top of the remembered column (after a vertical move). */
	if (spawn_cursor.mode == SPAWN_STACK_TOP && client_is_live(spawn_cursor.anchor)
			&& spawn_cursor.anchor != c && spawn_cursor.anchor->world.set) {
		c->world.x = spawn_cursor.anchor->world.x;
		c->world.y = column_min_y(m, c->world.x, c) - 1.0f;
		c->world.set = true;
		return;
	}

	/* Default: to the right of the currently-focused window. The new client is
	 * already at the front of fstack (mapnotify), so skip it to find the window
	 * that was focused when the spawn happened ("the current one"). */
	wl_list_for_each(w, &fstack, flink) {
		if (w == c)
			continue;
		if (!VISIBLEON(w, m) || w->isfloating || w->isfullscreen || !w->world.set)
			continue;
		f = w;
		break;
	}
	if (f) {
		/* An x just past the focused column so the left-to-right sort inserts it
		 * immediately to the right; arrange_columns compacts the real position. */
		c->world.x = f->world.x + f->geom.width * 0.5f + 1.0f;
		c->world.y = 0;
		c->world.set = true;
		return;
	}

	/* Fallback: first window / no focus -> rightmost new column (legacy). */
	col = get_rightmost_column(m);
	if (col.window_count == 0 && col.x == 0) {
		c->world.x = 0;
		c->world.y = 0;
	} else {
		c->world.x = col.right + COLUMN_GAP;
		c->world.y = 0;
	}
	c->world.set = true;
}

/* Move focused window to adjacent column in the horizontal strip (Niri-like). */
void
move_column(const Arg *arg)
{
	Client *c;
	Monitor *m;
	float col_x[256];
	float col_width[256];
	int col_count = 0;
	int i, dir, current = -1;
	float target_x;
	float right_edge;

	if (!selmon || !arg)
		return;

	c = focustop(selmon);
	if (!c || c->isfloating || c->isfullscreen)
		return;

	m = c->mon;
	if (!m || m->w.width <= 0 || m->w.height <= 0)
		return;

	if (!c->world.set)
		place_window_column(c, m);

	/* Collect columns and widths from visible windows. */
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		if (!c->world.set)
			place_window_column(c, m);
		for (i = 0; i < col_count; i++) {
			if (same_column_x(c->world.x, col_x[i]))
				break;
		}
		if (i == col_count && col_count < (int)LENGTH(col_x)) {
			col_x[col_count] = c->world.x;
			col_width[col_count] = 0;
			col_count++;
		}
		if (i < col_count && c->geom.width > col_width[i])
			col_width[i] = c->geom.width;
	}

	if (col_count == 0)
		return;

	/* Sort columns left-to-right. */
	for (i = 0; i < col_count; i++) {
		for (int j = i + 1; j < col_count; j++) {
			float tmp;
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

	/* Find current column index for focused client. */
	for (i = 0; i < col_count; i++) {
		if (same_column_x(focustop(selmon)->world.x, col_x[i])) {
			current = i;
			break;
		}
	}
	if (current < 0)
		return;

	dir = arg->i;
	if (dir < 0 && current == 0)
		return;

	if (dir > 0 && current == col_count - 1) {
		right_edge = col_x[current] + (col_width[current] > 0 ? col_width[current] : COLUMN_WIDTH);
		target_x = right_edge + COLUMN_GAP;
	} else {
		target_x = col_x[current + dir];
	}

	/* Move focused window to the target column top. */
	c = focustop(selmon);
	c->world.x = target_x;
	c->world.y = 0;
	c->world.set = true;
	arrange(selmon);
}

/* Same-column stack neighbour of c: the window directly above (up=1) or below. */
static Client *
stack_neighbor(Monitor *m, Client *c, int up)
{
	Client *w, *best = NULL;
	wl_list_for_each(w, &clients, link) {
		if (w == c || !VISIBLEON(w, m) || w->isfloating || w->isfullscreen || !w->world.set)
			continue;
		if (!same_column_x(w->world.x, c->world.x))
			continue;
		if (up) {
			if (w->world.y < c->world.y && (!best || w->world.y > best->world.y))
				best = w;
		} else {
			if (w->world.y > c->world.y && (!best || w->world.y < best->world.y))
				best = w;
		}
	}
	return best;
}

/* x of the column immediately left of c (nearest smaller distinct x). */
static int
left_column_x(Monitor *m, Client *c, float *out)
{
	Client *w;
	float best = 0;
	int found = 0;
	wl_list_for_each(w, &clients, link) {
		if (w == c || !VISIBLEON(w, m) || w->isfloating || w->isfullscreen || !w->world.set)
			continue;
		if (same_column_x(w->world.x, c->world.x) || w->world.x >= c->world.x)
			continue;
		if (!found || w->world.x > best) {
			best = w->world.x;
			found = 1;
		}
	}
	if (found)
		*out = best;
	return found;
}

/*
 * Carry the focused window through the grid (Super+Ctrl+Arrows).
 *  - up/down: reorder within the column stack; up at the top consumes into the
 *    left column, placed on top (the user's rule). down at the bottom is a no-op.
 *  - left/right: swap grid slots with the nearest window that way, so the window
 *    walks across the grid; with no neighbour, open a new column on that side.
 * Focus stays on the moved window so it visibly travels.
 */
void
move_window_dir(const Arg *arg)
{
	Client *c, *n, *w;
	Monitor *m;
	int dir;
	float leftx, tx, ty, edge;
	int have;

	if (!selmon || !arg)
		return;
	c = focustop(selmon);
	if (!c || c->isfloating || c->isfullscreen)
		return;
	m = c->mon;
	if (!m || m->w.width <= 0 || m->w.height <= 0)
		return;
	if (!c->world.set)
		place_window_column(c, m);
	dir = arg->i;

	if (dir == DIR_UP || dir == DIR_DOWN) {
		n = stack_neighbor(m, c, dir == DIR_UP);
		if (n) {
			ty = n->world.y;
			n->world.y = c->world.y;
			c->world.y = ty;
		} else if (dir == DIR_UP && left_column_x(m, c, &leftx)) {
			c->world.x = leftx;
			c->world.y = column_min_y(m, leftx, c) - 1.0f;
		} else {
			/* Top with no left column, or bottom edge: nothing to do. */
			return;
		}
		spawn_cursor_set_stack(c);
	} else {
		n = nearest_in_direction(c, dir);
		if (n) {
			tx = n->world.x;
			ty = n->world.y;
			n->world.x = c->world.x;
			n->world.y = c->world.y;
			c->world.x = tx;
			c->world.y = ty;
		} else {
			/* No neighbour that way: open a new column on that side. */
			edge = 0;
			have = 0;
			wl_list_for_each(w, &clients, link) {
				if (w == c || !VISIBLEON(w, m) || w->isfloating || w->isfullscreen || !w->world.set)
					continue;
				if (dir == DIR_RIGHT) {
					float r = w->world.x + w->geom.width;
					if (!have || r > edge) { edge = r; have = 1; }
				} else {
					if (!have || w->world.x < edge) { edge = w->world.x; have = 1; }
				}
			}
			if (dir == DIR_RIGHT)
				c->world.x = have ? edge + COLUMN_GAP : 0;
			else
				c->world.x = have ? edge - (c->geom.width + COLUMN_GAP) : 0;
			c->world.y = 0;
		}
		spawn_cursor.mode = SPAWN_RIGHT_OF_FOCUS;
	}

	c->world.set = true;
	arrange(selmon);
}

/* Reset the adaptive spawn cursor when focus lands outside its column. */
void
spawn_cursor_on_focus(Client *c)
{
	if (spawn_cursor.mode != SPAWN_STACK_TOP)
		return;
	if (!c || !client_is_live(spawn_cursor.anchor)
			|| !same_column_x(c->world.x, spawn_cursor.anchor->world.x))
		spawn_cursor.mode = SPAWN_RIGHT_OF_FOCUS;
}

/* Arrange windows: niri-style vertical stacking in columns */
void
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

	if (!m || m->w.width <= 0 || m->w.height <= 0)
		return;

	col_count = 0;
	placed_count = 0;
	
	/* Pass 1: ensure valid client geometry and collect columns with max width. */
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
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
				if (!same_column_x(c->world.x, col_x[i]))
					continue;

				already_placed = 0;
				for (int k = 0; k < placed_count; k++) {
					if (placed[k] == c) {
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
			col_next_y[i] += best->geom.height + WINDOW_GAP;
			if (placed_count < (int)LENGTH(placed))
				placed[placed_count++] = best;
		}
	}

	/* Pass 3: apply geometry to scene. */
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
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

		/* Set the layout target; the compositor springs the window there so
		 * columns slide instead of snapping (camera pans stay instant). */
		client_set_target_geom(c, geo);
	}
}

void
infinite(Monitor *m)
{
	Client *c;
	Client *new_client = NULL;
	static int in_infinite_arrange = 0;

	if (!m || m->w.width <= 0 || m->w.height <= 0)
		return;

	if (in_infinite_arrange)
		return;
	in_infinite_arrange = 1;

	/* Track a newly mapped tiled client before arrangement so follow_new_windows
	 * can center on the actual new window only. */
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen && !c->world.set) {
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

	in_infinite_arrange = 0;
}
