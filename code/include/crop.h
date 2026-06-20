/*
 * Crop mode module for dwl
 * 
 * This module provides an interactive crop mode that allows users to select
 * a region of a window to crop it to. The crop mode creates an overlay on
 * the screen and lets the user drag to define a selection rectangle.
 */

#ifndef CROP_H
#define CROP_H

#include <stdbool.h>

/* Forward declarations - these are defined in dwl.c */
struct wlr_scene_rect;
struct wlr_cursor;
struct wlr_scene_tree;
struct wlr_box;
struct Client;
struct Monitor;
union Arg;

/*
 * Scene layer indices - must match dwl.c's Lyr* enum
 */
enum {
	LyrBg,
	LyrBottom,
	LyrTile,
	LyrFloat,
	LyrTop,
	LyrFS,
	LyrOverlay,
	LyrBlock,
	NUM_LAYERS
};

/*
 * Crop editor state structure
 * 
 * Maintains the complete state of the crop mode including:
 * - The target client being cropped
 * - Selection coordinates (start and end positions)
 * - Visual elements (overlay and selection rectangles)
 * 
 * This is the global state that persists throughout the crop operation.
 * The workflow is:
 * 1. cropbegin() initializes this state and creates visual elements
 * 2. User drags to select region; cropdraw() updates visuals
 * 3. cropend() applies the crop and cropcancel() cleans up
 */
struct CropEditor {
	bool active;           /* Whether crop mode is currently active */
	struct Client *target; /* The client being cropped */
	double start_x;        /* Selection start X (global coordinates) */
	double start_y;        /* Selection start Y (global coordinates) */
	double end_x;          /* Selection end X (global coordinates) */
	double end_y;          /* Selection end Y (global coordinates) */
	bool dragging;         /* Whether user is currently dragging selection */
	struct wlr_scene_rect *overlay;      /* Dark fullscreen overlay */
	struct wlr_scene_rect *selection;    /* Selection rectangle border */
	struct wlr_scene_rect *selection_bg; /* Selection rectangle fill */
};

/* Global crop editor state - defined in crop.c, visible to dwl.c */
extern struct CropEditor crop_editor;

/* Scene layers for overlay placement - defined in dwl.c */
extern struct wlr_scene_tree *layers[];

/* Global cursor for position tracking - defined in dwl.c */
extern struct wlr_cursor *cursor;

/* Selected monitor - defined in dwl.c */
extern struct Monitor *selmon;

/* Function declarations */

/*
 * Begin crop mode for the currently focused client.
 * 
 * Creates the overlay UI consisting of:
 * - A dark semi-transparent overlay covering the entire monitor
 * - Selection rectangle visual elements (initially hidden)
 * 
 * Enters crop selection mode where the user can drag to define
 * a crop region. The crop will be applied to the focused client.
 * 
 * @param arg: Command argument (unused)
 */
void cropbegin(const union Arg *arg);

/*
 * Cancel crop mode without applying any crop.
 * 
 * Destroys the overlay UI elements created by cropbegin() and
 * resets the crop_editor state to inactive. Safe to call even
 * if crop mode is not currently active.
 * 
 * @param arg: Command argument (unused)
 */
void cropcancel(const union Arg *arg);

/*
 * End crop mode and apply the selected crop region.
 * 
 * Calculates the crop region in normalized coordinates [0-1] relative
 * to the window, stores these in the client's crop state, and resizes
 * the window to match the crop dimensions.
 * 
 * The crop values are clamped to valid bounds:
 * - Position (x, y): clamped to [0, 1]
 * - Size (w, h): minimum 0.1 (10%), maximum remaining window space
 * 
 * After applying the crop, cleans up the UI via cropcancel().
 * 
 * @param arg: Command argument (unused)
 */
void cropend(const union Arg *arg);

/*
 * Update the visual selection rectangle during dragging.
 * 
 * Called during pointer motion events (from buttonpress() in dwl.c)
 * while the user is dragging to define the crop region. Updates the
 * position and size of the selection rectangles to match the current
 * drag state.
 * 
 * The selection has a minimum size of 10x10 pixels to ensure
 * the visual feedback is always visible.
 */
void cropdraw(void);

/*
 * Get the top focused client on a monitor.
 * Defined in dwl.c, needed by cropbegin.
 * 
 * @param m: The monitor to check
 * @return: The top focused client, or NULL if none
 */
struct Client *focustop(struct Monitor *m);

/*
 * Resize a client to the specified geometry.
 * Defined in dwl.c, needed by cropend.
 * 
 * @param c: The client to resize
 * @param geo: The new geometry (position and size)
 * @param interact: Whether this is an interactive resize
 */
void resize(struct Client *c, struct wlr_box geo, int interact);

#endif /* CROP_H */
