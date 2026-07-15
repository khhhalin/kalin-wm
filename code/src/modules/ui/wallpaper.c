/* World wallpaper system (repeating/tiled), anchored to world coordinates.
 *
 * Separately-compiled TU: owns the shared `wallpaper` scene state, reads the
 * `viewport` camera, and links against dwl.c's layers[]/ecalloc via kalin.h. */
#include "kalin.h"

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
	 * "Blue room" repeating tile, recolored to a warm amber/ochre "study"
	 * palette (oranges/yellows/browns) to match the rest of the rice — same
	 * original geometric pattern, just retinted (see the ledger for when).
	 */
	wall_h = (int)(tile_size * 0.68f);
	floor_h = tile_size - wall_h;
	baseboard_h = 6;
	stripe_w = 10;

	/* Base */
	r = wlr_scene_rect_create(tile, tile_size, tile_size, (float[]){0.09f, 0.06f, 0.03f, 1.0f});
	wlr_scene_node_set_position(&r->node, 0, 0);

	/* Wall */
	r = wlr_scene_rect_create(tile, tile_size, wall_h, (float[]){0.22f, 0.14f, 0.07f, 1.0f});
	wlr_scene_node_set_position(&r->node, 0, 0);

	/* Wall panel stripes */
	for (x = 0; x < tile_size; x += 80) {
		r = wlr_scene_rect_create(tile, stripe_w, wall_h, (float[]){0.29f, 0.18f, 0.08f, 1.0f});
		wlr_scene_node_set_position(&r->node, x + 18, 0);
		r = wlr_scene_rect_create(tile, 2, wall_h, (float[]){0.12f, 0.07f, 0.03f, 0.85f});
		wlr_scene_node_set_position(&r->node, x + 18 + stripe_w + 6, 0);
	}

	/* Baseboard */
	r = wlr_scene_rect_create(tile, tile_size, baseboard_h, (float[]){0.30f, 0.19f, 0.09f, 1.0f});
	wlr_scene_node_set_position(&r->node, 0, wall_h - baseboard_h);

	/* Floor */
	r = wlr_scene_rect_create(tile, tile_size, floor_h, (float[]){0.24f, 0.14f, 0.06f, 1.0f});
	wlr_scene_node_set_position(&r->node, 0, wall_h);

	/* Floor plank lines */
	for (x = 0; x < tile_size; x += 48) {
		r = wlr_scene_rect_create(tile, 2, floor_h, (float[]){0.12f, 0.07f, 0.03f, 0.55f});
		wlr_scene_node_set_position(&r->node, x + 24, wall_h);
	}

	/* Framed "window" on wall */
	r = wlr_scene_rect_create(tile, 170, 120, (float[]){0.94f, 0.78f, 0.47f, 1.0f});
	wlr_scene_node_set_position(&r->node, 96, 72);
	r = wlr_scene_rect_create(tile, 170, 6, (float[]){0.82f, 0.55f, 0.16f, 1.0f});
	wlr_scene_node_set_position(&r->node, 96, 72);
	r = wlr_scene_rect_create(tile, 6, 120, (float[]){0.82f, 0.55f, 0.16f, 1.0f});
	wlr_scene_node_set_position(&r->node, 96, 72);
	r = wlr_scene_rect_create(tile, 170, 6, (float[]){0.82f, 0.55f, 0.16f, 1.0f});
	wlr_scene_node_set_position(&r->node, 96, 72 + 120 - 6);
	r = wlr_scene_rect_create(tile, 6, 120, (float[]){0.82f, 0.55f, 0.16f, 1.0f});
	wlr_scene_node_set_position(&r->node, 96 + 170 - 6, 72);

	/* Rug */
	r = wlr_scene_rect_create(tile, 260, 120, (float[]){0.55f, 0.28f, 0.10f, 0.85f});
	wlr_scene_node_set_position(&r->node, 110, wall_h + (floor_h / 2) - 60);
	r = wlr_scene_rect_create(tile, 260, 4, (float[]){0.30f, 0.16f, 0.06f, 0.85f});
	wlr_scene_node_set_position(&r->node, 110, wall_h + (floor_h / 2) - 60);
	r = wlr_scene_rect_create(tile, 260, 4, (float[]){0.30f, 0.16f, 0.06f, 0.85f});
	wlr_scene_node_set_position(&r->node, 110, wall_h + (floor_h / 2) + 56);

	/* Corner vignette */
	r = wlr_scene_rect_create(tile, tile_size, 24, (float[]){0.0f, 0.0f, 0.0f, 0.18f});
	wlr_scene_node_set_position(&r->node, 0, 0);
	r = wlr_scene_rect_create(tile, 24, tile_size, (float[]){0.0f, 0.0f, 0.0f, 0.18f});
	wlr_scene_node_set_position(&r->node, 0, 0);
}

void
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

void
wallpaper_update(void)
{
	static int last_base_x, last_base_y;
	static int have_last = 0;
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
	/* One shared wallpaper tree can only follow one camera; track the cursor's
	 * monitor (multi-camera). Per-monitor wallpaper is deferred polish — see
	 * obsidian/multi-camera.md. */
	cam_x = selmon ? selmon->cam.x : 0.0f;
	cam_y = selmon ? selmon->cam.y : 0.0f;

	/* World-anchored background: apply the same world->screen transform as windows.
	 * Sub-tile camera motion must move this every call, so this can't be cached. */
	wlr_scene_node_set_position(&wallpaper.tree->node, (int)(-cam_x), (int)(-cam_y));

	base_x = (int)floorf(cam_x / (float)tile_size) - 1;
	base_y = (int)floorf(cam_y / (float)tile_size) - 1;

	/* Tiles only need repositioning when the camera crosses a tile boundary
	 * (base_x/base_y changes) — called every frame from arrange(), so skip the
	 * loop entirely otherwise. */
	if (have_last && base_x == last_base_x && base_y == last_base_y)
		return;
	last_base_x = base_x;
	last_base_y = base_y;
	have_last = 1;

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
