#include "include/viewport.h"
#include "include/kalin.h"
#include <stdlib.h>
#include <math.h>

/* Global viewport state */
struct Viewport viewport = { 0, 0, 1.0f, 0 };
struct Wallpaper wallpaper = { NULL, NULL, 0 };

/* External dependencies */
extern struct wl_list mons;
extern Monitor *selmon;
extern struct wlr_scene_tree *layers[];
extern void arrange(Monitor *m);
extern Client *focustop(Monitor *m);

/* Colors */
static const float wallbg_rgba[4] = {0.1f, 0.1f, 0.15f, 1.0f};
static const float wallpattern_rgba[4] = {0.15f, 0.15f, 0.2f, 1.0f};

void
viewport_init(void)
{
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.zoom = 1.0f;
	viewport.follow = 0;
}

void
viewport_pan(const float dx, const float dy)
{
	float z;

	/* Pan speed is inverse to zoom (faster when zoomed out) */
	z = viewport.zoom > 0.0f ? viewport.zoom : 1.0f;
	viewport.x += dx / z;
	viewport.y += dy / z;
	
	if (selmon)
		arrange(selmon);
}

void
viewport_zoom(float factor)
{
	if (factor <= 0.0f)
		return;
	
	viewport.zoom *= factor;
	
	/* Clamp zoom range */
	if (viewport.zoom < 0.1f)
		viewport.zoom = 0.1f;
	if (viewport.zoom > 5.0f)
		viewport.zoom = 5.0f;
	
	if (selmon)
		arrange(selmon);
}

void
viewport_reset(void)
{
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.zoom = 1.0f;
	
	if (selmon)
		arrange(selmon);
}

void
viewport_center_on(Client *c)
{
	Monitor *m;
	float z;
	if (!c || !c->mon)
		return;
	
	m = c->mon;
	z = viewport.zoom > 0.0f ? viewport.zoom : 1.0f;
	
	/* Center the camera so the window appears in the middle of the monitor */
	viewport.x = c->world.x + c->geom.width / 2.0f - m->w.width / (2.0f * z);
	viewport.y = c->world.y + c->geom.height / 2.0f - m->w.height / (2.0f * z);
	
	arrange(m);
}

void
viewport_toggle_follow(void)
{
	viewport.follow = !viewport.follow;
	
	if (viewport.follow && selmon) {
		Client *c = focustop(selmon);
		if (c && c->world.set)
			viewport_center_on(c);
	}
}

void
viewport_follow_focus(void)
{
	Client *c;
	
	if (!viewport.follow || !selmon)
		return;
	
	c = focustop(selmon);
	if (c && c->world.set) {
		viewport_center_on(c);
	}
}

void
wallpaper_init(void)
{
	Monitor *m;
	int w, h, grid_size, max_lines, line_count, x, y;
	
	/* Use selmon if available, otherwise use first monitor */
	if (selmon)
		m = selmon;
	else if (!wl_list_empty(&mons))
		m = wl_container_of(mons.next, m, link);
	else
		return;
	
	w = m->m.width;
	h = m->m.height;
	
	/* Clean up old wallpaper */
	if (wallpaper.bg) {
		wlr_scene_node_destroy(&wallpaper.bg->node);
		wallpaper.bg = NULL;
	}
	for (int i = 0; i < wallpaper.num_lines; i++) {
		if (wallpaper.lines[i])
			wlr_scene_node_destroy(&wallpaper.lines[i]->node);
	}
	free(wallpaper.lines);
	
	/* Create stationary background - stays fixed regardless of viewport */
	wallpaper.bg = wlr_scene_rect_create(layers[LyrBg], w, h, wallbg_rgba);
	wlr_scene_node_set_position(&wallpaper.bg->node, 0, 0);
	
	/* Grid pattern - stationary reference that never moves */
	grid_size = 100;
	max_lines = (w / grid_size + h / grid_size + 4) * 2;
	wallpaper.lines = ecalloc(max_lines, sizeof(struct wlr_scene_rect *));
	line_count = 0;
	
	/* Vertical lines */
	for (x = 0; x <= w && line_count < max_lines; x += grid_size) {
		wallpaper.lines[line_count] = wlr_scene_rect_create(
			layers[LyrBg], 2, h, wallpattern_rgba);
		wlr_scene_node_set_position(&wallpaper.lines[line_count]->node, x - 1, 0);
		line_count++;
	}
	
	/* Horizontal lines */
	for (y = 0; y <= h && line_count < max_lines; y += grid_size) {
		wallpaper.lines[line_count] = wlr_scene_rect_create(
			layers[LyrBg], w, 2, wallpattern_rgba);
		wlr_scene_node_set_position(&wallpaper.lines[line_count]->node, 0, y - 1);
		line_count++;
	}
	
	wallpaper.num_lines = line_count;
}

void
wallpaper_cleanup(void)
{
	if (wallpaper.bg) {
		wlr_scene_node_destroy(&wallpaper.bg->node);
		wallpaper.bg = NULL;
	}
	for (int i = 0; i < wallpaper.num_lines; i++) {
		if (wallpaper.lines[i])
			wlr_scene_node_destroy(&wallpaper.lines[i]->node);
	}
	free(wallpaper.lines);
	wallpaper.lines = NULL;
	wallpaper.num_lines = 0;
}
