/*
 * Crop mode module implementation
 * 
 * Provides interactive crop functionality for windows.
 * The crop workflow is:
 * 1. User invokes cropbegin() - creates overlay, enters crop mode
 * 2. User clicks and drags to define selection rectangle
 * 3. On mouse release, cropend() calculates and applies the crop
 * 4. User can cancel at any time with cropcancel()
 */

#include <math.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

/* Include main project header for type definitions (Client, Monitor, etc.) */
#include "include/kalin.h"
#include "include/crop.h"

/* Utility macros */
#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))

/* Global crop editor state */
struct CropEditor crop_editor = {0};

/*
 * Begin crop mode for the currently focused client.
 * 
 * Creates three visual elements:
 * - overlay: A dark semi-transparent rectangle covering the entire monitor
 * - selection_bg: The fill of the selection rectangle (semi-transparent blue)
 * - selection: The border of the selection rectangle (blue outline)
 * 
 * The selection rectangles are initially hidden and will be shown
 * and updated during dragging via cropdraw().
 */
void
cropbegin(const union Arg *arg)
{
	if (!selmon)
		return;

	/* Get the currently focused client on the selected monitor */
	struct Client *c = focustop(selmon);
	/* Don't start if there's no client or crop mode is already active */
	if (!c || crop_editor.active) return;
	
	crop_editor.active = true;
	crop_editor.target = c;
	crop_editor.dragging = false;
	
	/* Create dark overlay covering entire monitor output */
	struct Monitor *m = selmon;
	crop_editor.overlay = wlr_scene_rect_create(layers[7], /* LyrOverlay */
		m->m.width, m->m.height, (float[]){0, 0, 0, 0.7});
	wlr_scene_node_set_position(&crop_editor.overlay->node, m->m.x, m->m.y);
	
	/* Create selection rectangle fill (semi-transparent blue) */
	crop_editor.selection_bg = wlr_scene_rect_create(layers[7], /* LyrOverlay */
		0, 0, (float[]){0.2, 0.5, 0.9, 0.3});
	
	/* Create selection rectangle border (blue outline, transparent fill) */
	crop_editor.selection = wlr_scene_rect_create(layers[7], /* LyrOverlay */
		0, 0, (float[]){0.2, 0.5, 0.9, 0});
	
	/* Hide selection rectangles until dragging starts */
	wlr_scene_node_set_enabled(&crop_editor.selection_bg->node, false);
	wlr_scene_node_set_enabled(&crop_editor.selection->node, false);
}

/*
 * Cancel crop mode without applying changes.
 * 
 * Destroys all visual elements created by cropbegin() and resets
 * the crop_editor state to inactive. Safe to call even if crop
 * mode is not active.
 */
void
cropcancel(const union Arg *arg)
{
	if (!crop_editor.active) return;
	
	/* Destroy all visual elements */
	if (crop_editor.overlay)
		wlr_scene_node_destroy(&crop_editor.overlay->node);
	if (crop_editor.selection_bg)
		wlr_scene_node_destroy(&crop_editor.selection_bg->node);
	if (crop_editor.selection)
		wlr_scene_node_destroy(&crop_editor.selection->node);
	crop_editor.overlay = NULL;
	crop_editor.selection_bg = NULL;
	crop_editor.selection = NULL;
	
	/* Reset state */
	crop_editor.active = false;
	crop_editor.target = NULL;
	crop_editor.dragging = false;
}

/*
 * End crop mode and apply the selected crop region.
 * 
 * Calculates the crop region in normalized coordinates [0-1] relative
 * to the window, stores these in the client's crop state, and resizes
 * the window to match the crop dimensions.
 * 
 * The crop values are clamped to ensure they stay within valid bounds:
 * - Crop position (x, y): clamped to [0, 1]
 * - Crop size (w, h): clamped to minimum 0.1, maximum remaining window
 */
void
cropend(const union Arg *arg)
{
	/* If not actively dragging, just cancel */
	if (!crop_editor.active || !crop_editor.dragging) {
		cropcancel(arg);
		return;
	}
	
	struct Client *c = crop_editor.target;
	if (c) {
		/* Calculate selection rectangle in screen coordinates */
		/* Use MIN/MAX to handle drag in any direction */
		int sx = crop_editor.start_x < crop_editor.end_x ? 
		         crop_editor.start_x : crop_editor.end_x;
		int sy = crop_editor.start_y < crop_editor.end_y ? 
		         crop_editor.start_y : crop_editor.end_y;
		int ex = crop_editor.start_x > crop_editor.end_x ? 
		         crop_editor.start_x : crop_editor.end_x;
		int ey = crop_editor.start_y > crop_editor.end_y ? 
		         crop_editor.start_y : crop_editor.end_y;
		int w = ex - sx;
		int h = ey - sy;
		
		/* Get window geometry (position and size) */
		int wx = c->geom.x;
		int wy = c->geom.y;
		int ww = c->geom.width;
		int wh = c->geom.height;

		if (ww <= 0 || wh <= 0) {
			cropcancel(arg);
			return;
		}
		
		/* Calculate crop as normalized values relative to window */
		float cx = (float)(sx - wx) / ww;
		float cy = (float)(sy - wy) / wh;
		float cw = (float)w / ww;
		float ch = (float)h / wh;
		
		/* Clamp crop position to valid range [0, 1] */
		if (cx < 0) cx = 0;
		if (cx > 1) cx = 1;
		if (cy < 0) cy = 0;
		if (cy > 1) cy = 1;
		
		/* Clamp crop size: minimum 10% of window, maximum remaining space */
		if (cw < 0.1f) cw = 0.1f;
		if (cw > 1.0f - cx) cw = 1.0f - cx;
		if (ch < 0.1f) ch = 0.1f;
		if (ch > 1.0f - cy) ch = 1.0f - cy;
		
		/* Store crop state in client */
		c->crop.active = true;
		c->crop.x = cx;
		c->crop.y = cy;
		c->crop.w = cw;
		c->crop.h = ch;
		
		/* Resize window to match crop dimensions */
		struct wlr_box newgeo = {
			.x = wx,
			.y = wy,
			.width = (int)(ww * cw),
			.height = (int)(wh * ch)
		};
		resize(c, newgeo, 0);
	}
	
	/* Clean up crop mode */
	cropcancel(arg);
}

/*
 * Update the visual selection rectangle during dragging.
 * 
 * Called during pointer motion events while the user is dragging
 * to define the crop region. Updates the position and size of
 * the selection rectangles to match the current drag state.
 * 
 * The selection has a minimum size of 10x10 pixels to ensure
 * the visual feedback is always visible.
 */
void
cropdraw(void)
{
	if (!crop_editor.active || !crop_editor.dragging) return;
	if (!crop_editor.selection_bg || !crop_editor.selection)
		return;
	
	/* Calculate rectangle from drag coordinates */
	int x = crop_editor.start_x < crop_editor.end_x ? 
	        crop_editor.start_x : crop_editor.end_x;
	int y = crop_editor.start_y < crop_editor.end_y ? 
	        crop_editor.start_y : crop_editor.end_y;
	int w = (int)fabs(crop_editor.end_x - crop_editor.start_x);
	int h = (int)fabs(crop_editor.end_y - crop_editor.start_y);
	
	/* Ensure minimum size for visibility */
	if (w < 10) w = 10;
	if (h < 10) h = 10;
	
	/* Update selection fill rectangle size */
	wlr_scene_rect_set_size(crop_editor.selection_bg, w, h);
	/* Update selection border (slightly larger to create outline effect) */
	wlr_scene_rect_set_size(crop_editor.selection, w + 4, h + 4);
	
	/* Position the selection rectangles */
	wlr_scene_node_set_position(&crop_editor.selection_bg->node, x, y);
	/* Border is offset by 2 pixels to center around the fill */
	wlr_scene_node_set_position(&crop_editor.selection->node, x - 2, y - 2);
	
	/* Make selection visible */
	wlr_scene_node_set_enabled(&crop_editor.selection_bg->node, true);
	wlr_scene_node_set_enabled(&crop_editor.selection->node, true);
}
