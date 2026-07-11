/* Standalone logic tests for the pure-math parts of
 * code/src/modules/viewport/viewport_ops.c: viewport_ensure_visible()'s
 * "pan the minimum amount, not to center" computation, and
 * viewport_menu_reveal()'s "how much extra pan does the hold-Super arc menu
 * need" computation.
 *
 * viewport_ops.c can't be linked standalone (it needs a running wlroots
 * backend via kalin.h's Monitor/Client), so this test reimplements just the
 * geometry math against minimal stand-ins, the same technique
 * test_growth_overlap.c and test_connection_graph.c already use. The real
 * functions' side effects (viewport_move_to()/viewport_pan(), which touch
 * animation state and kick a frame) are irrelevant to what's being verified
 * here — only the resulting camera position/pan amount is. Keep this in
 * sync with viewport_ops.c if that math changes. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

struct wlr_box { int x, y, width, height; };
typedef struct Monitor { struct wlr_box w; } Monitor;
typedef struct Client { struct wlr_box geom; Monitor *mon; } Client;

static struct { float x, y, zoom; } viewport = { 0, 0, 1.0f };

/* Mirrors viewport_ensure_visible()'s math exactly, minus the
 * viewport_move_to()/viewport_camera_tick() side effects — returns the
 * camera position it would move to via out-params, and whether it would
 * move at all. */
static int
ensure_visible(Client *c, float *out_x, float *out_y)
{
	Monitor *m;
	float z, vw, vh;
	float wx0, wy0, wx1, wy1;
	float nx, ny;

	m = c->mon;
	z = viewport.zoom > 0.0f ? viewport.zoom : 1.0f;
	vw = m->w.width / z;
	vh = m->w.height / z;

	wx0 = c->geom.x;
	wy0 = c->geom.y;
	wx1 = c->geom.x + c->geom.width;
	wy1 = c->geom.y + c->geom.height;

	nx = viewport.x;
	ny = viewport.y;

	if (wx1 - wx0 > vw || wx0 < viewport.x)
		nx = wx0;
	else if (wx1 > viewport.x + vw)
		nx = wx1 - vw;

	if (wy1 - wy0 > vh || wy0 < viewport.y)
		ny = wy0;
	else if (wy1 > viewport.y + vh)
		ny = wy1 - vh;

	*out_x = nx;
	*out_y = ny;
	return !(nx == viewport.x && ny == viewport.y);
}

#define MENU_ARC_RESERVE_PX 300

/* Mirrors viewport_menu_reveal()'s math exactly, minus the viewport_pan()
 * side effect — returns the screen-space pan amount it would request (0 if
 * none), and whether it skipped due to the dock-mode width threshold. */
static float
menu_reveal_pan_amount(Client *c, int *skipped_dock_mode)
{
	Monitor *m = c->mon;
	float z = viewport.zoom > 0.0f ? viewport.zoom : 1.0f;
	float win_w_screen = c->geom.width * z;
	float win_right_screen, overflow;

	*skipped_dock_mode = 0;
	if (win_w_screen >= m->w.width * 0.85f) {
		*skipped_dock_mode = 1;
		return 0.0f;
	}

	win_right_screen = (c->geom.x - viewport.x) * z + win_w_screen;
	overflow = win_right_screen + MENU_ARC_RESERVE_PX - m->w.width;
	return overflow > 0.0f ? overflow : 0.0f;
}

static int failures = 0;

#define CHECK(cond, msg) do { \
	if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
} while (0)

static void
reset_viewport(void)
{
	viewport.x = 0;
	viewport.y = 0;
	viewport.zoom = 1.0f;
}

static void
test_ensure_visible_noop_when_fully_visible(void)
{
	Monitor m = { {0, 0, 1280, 800} };
	Client c = { {100, 100, 300, 200}, &m };
	float nx, ny;
	int moved;

	printf("Running ensure_visible_noop_when_fully_visible...\n");
	reset_viewport();
	moved = ensure_visible(&c, &nx, &ny);
	CHECK(!moved, "camera must not move when the window is already fully on screen");
	CHECK(nx == 0.0f && ny == 0.0f, "reported position should equal the unchanged viewport origin");
	printf(!moved ? "  PASS\n" : "  (see failures above)\n");
}

static void
test_ensure_visible_aligns_to_right_edge(void)
{
	/* Window's right edge (1400) is past the 1280-wide viewport; ensure_visible
	 * should align the window's right edge to the viewport's right edge,
	 * not center the window in the middle of the screen. */
	Monitor m = { {0, 0, 1280, 800} };
	Client c = { {1100, 100, 300, 200}, &m }; /* right edge at 1400 */
	float nx, ny;
	int moved;

	printf("Running ensure_visible_aligns_to_right_edge...\n");
	reset_viewport();
	moved = ensure_visible(&c, &nx, &ny);
	CHECK(moved, "camera should move since the window's right edge is off screen");
	/* new viewport.x + vw(1280) should equal the window's right edge (1400) */
	CHECK(nx == 1400.0f - 1280.0f, "viewport.x should align so the window's right edge is exactly on screen");
	printf(moved && nx == 120.0f ? "  PASS\n" : "  (see failures above)\n");
}

static void
test_ensure_visible_aligns_to_left_edge_not_center(void)
{
	/* Window sits to the left of the current viewport entirely; should align
	 * to the window's left edge (scroll left just enough), not jump to
	 * center it in the middle of the screen. */
	Monitor m = { {0, 0, 1280, 800} };
	Client c = { {-500, 0, 300, 200}, &m };
	float nx, ny;
	int moved;

	printf("Running ensure_visible_aligns_to_left_edge_not_center...\n");
	viewport.x = 0; viewport.y = 0; viewport.zoom = 1.0f;
	moved = ensure_visible(&c, &nx, &ny);
	CHECK(moved, "camera should move since the window is entirely off screen to the left");
	CHECK(nx == -500.0f, "viewport.x should align exactly to the window's left edge");
	/* Explicitly NOT centering: center would be -500 + 150 - 640 = -990. */
	CHECK(nx != -990.0f, "must not center the window in the middle of the screen");
	printf(moved && nx == -500.0f ? "  PASS\n" : "  (see failures above)\n");
}

static void
test_ensure_visible_oversized_window_aligns_near_edge(void)
{
	/* Window wider than the viewport: can't fit both edges, so align to the
	 * near (left) edge rather than splitting the difference. */
	Monitor m = { {0, 0, 1280, 800} };
	Client c = { {50, 0, 2000, 200}, &m };
	float nx, ny;
	int moved;

	printf("Running ensure_visible_oversized_window_aligns_near_edge...\n");
	reset_viewport();
	moved = ensure_visible(&c, &nx, &ny);
	CHECK(moved, "camera should move to bring the oversized window's near edge into view");
	CHECK(nx == 50.0f, "should align to the window's left edge, not attempt to center a too-wide window");
	printf(moved && nx == 50.0f ? "  PASS\n" : "  (see failures above)\n");
}

static void
test_menu_reveal_noop_when_room_available(void)
{
	Monitor m = { {0, 0, 1280, 800} };
	Client c = { {100, 100, 300, 200}, &m }; /* right edge at 400, tons of room */
	int skipped;
	float pan;

	printf("Running menu_reveal_noop_when_room_available...\n");
	reset_viewport();
	pan = menu_reveal_pan_amount(&c, &skipped);
	CHECK(!skipped, "should not be skipped for dock-mode reasons");
	CHECK(pan == 0.0f, "no pan needed when there's already room for the arc");
	printf(pan == 0.0f ? "  PASS\n" : "  (see failures above)\n");
}

static void
test_menu_reveal_pans_when_near_right_edge(void)
{
	/* Window right edge at 1200 (only 80px clear on a 1280-wide screen);
	 * needs 300px reserve, so should request a pan of 1200+300-1280 = 220. */
	Monitor m = { {0, 0, 1280, 800} };
	Client c = { {900, 100, 300, 200}, &m };
	int skipped;
	float pan;

	printf("Running menu_reveal_pans_when_near_right_edge...\n");
	reset_viewport();
	pan = menu_reveal_pan_amount(&c, &skipped);
	CHECK(!skipped, "300px-wide window on a 1280 monitor is well under the 85% dock threshold");
	CHECK(pan == 220.0f, "should request exactly enough pan to clear the arc's reserve");
	printf(pan == 220.0f ? "  PASS\n" : "  (see failures above)\n");
}

static void
test_menu_reveal_skips_full_width_window(void)
{
	/* Window >= 85% of monitor width: shell already docks the menu to the
	 * screen edge, so no camera pan should be requested regardless of the
	 * window's own position. */
	Monitor m = { {0, 0, 1280, 800} };
	Client c = { {50, 100, 1150, 200}, &m }; /* 1150/1280 = 89.8% */
	int skipped;
	float pan;

	printf("Running menu_reveal_skips_full_width_window...\n");
	reset_viewport();
	pan = menu_reveal_pan_amount(&c, &skipped);
	CHECK(skipped, "should be skipped: window is above the 85% dock-mode width threshold");
	CHECK(pan == 0.0f, "no pan should be requested for an already-docked window");
	printf(skipped && pan == 0.0f ? "  PASS\n" : "  (see failures above)\n");
}

int
main(void)
{
	test_ensure_visible_noop_when_fully_visible();
	test_ensure_visible_aligns_to_right_edge();
	test_ensure_visible_aligns_to_left_edge_not_center();
	test_ensure_visible_oversized_window_aligns_near_edge();
	test_menu_reveal_noop_when_room_available();
	test_menu_reveal_pans_when_near_right_edge();
	test_menu_reveal_skips_full_width_window();

	printf("\n===================================\n");
	printf("Total assertion failures: %d\n", failures);
	printf("===================================\n");
	return failures ? 1 : 0;
}
