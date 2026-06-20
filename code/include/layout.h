/*
 * Layout interface and common declarations for kalin-wm.
 * Each layout implements the arrange function that positions clients on a monitor.
 */

#ifndef LAYOUT_H
#define LAYOUT_H

#include <wayland-server-core.h>
#include <wlr/util/box.h>
#include <wlr/types/wlr_scene.h>

/* Forward declarations */
typedef struct Client Client;
typedef struct Monitor Monitor;

/* Layout function signature - arranges clients on the given monitor */
typedef void (*ArrangeFunc)(Monitor *);

/* Layout definition */
typedef struct {
	const char *symbol;    /* Symbol displayed in status bar (e.g., "[]=") */
	ArrangeFunc arrange;   /* Function to arrange windows in this layout */
} Layout;

/* Client structure - matches dwl.c definition */
struct Client {
	/* Must keep this field first */
	unsigned int type; /* XDGShell or X11* */
	
	/* Crop state */
	struct {
		int active;
		float x, y;      /* normalized [0-1] */
		float w, h;      /* normalized [0-1] */
	} crop;
	
	/* World coordinates for infinite layout - persistent position in the canvas */
	struct {
		float x, y;      /* world position (not screen position) */
		int set;         /* true if world position has been assigned */
	} world;

	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_rect *border[4]; /* top, bottom, left, right */
	struct wlr_scene_rect *focus_ring[4]; /* top, bottom, left, right */
	struct wlr_scene_tree *scene_surface;
	struct wl_list link;
	struct wl_list flink;
	struct wlr_box geom; /* layout-relative, includes border */
	struct wlr_box prev; /* layout-relative, includes border */
	struct wlr_box bounds; /* only width and height are used */
	union {
		struct wlr_xdg_surface *xdg;
		struct wlr_xwayland_surface *xwayland;
	} surface;
	
	unsigned int tags;
	int isfloating;
	int isfullscreen;
	int isurgent;
	unsigned int bw;     /* border width */
};

/* Monitor structure - matches dwl.c definition */
struct Monitor {
	struct wl_list link;
	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;
	struct wlr_scene_rect *fullscreen_bg;
	struct wl_listener frame;
	struct wl_listener destroy;
	struct wl_listener request_state;
	struct wl_listener destroy_lock_surface;
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct wlr_box m; /* monitor area, layout-relative */
	struct wlr_box w; /* window area, layout-relative */
	struct wl_list layers[4]; /* LayerSurface.link */
	const Layout *lt[2];
	unsigned int seltags;
	unsigned int sellt;
	unsigned int tagset[2];
	float mfact;
	int gamma_lut_changed;
	int nmaster;
	char ltsymbol[16];
	int asleep;
};

/* External variables needed by layouts */
extern struct wl_list clients;  /* Tiling order list of all clients */

/* Helper macros */
#define VISIBLEON(C, M) ((C) && (M) && (C)->mon == (M) && ((C)->tags & (M)->tagset[(M)->seltags]))
#define LENGTH(X)       (sizeof X / sizeof X[0])
#define MIN(A, B)       ((A) < (B) ? (A) : (B))

/* Utility functions provided by the window manager */
struct Client *focustop(Monitor *m);
void resize(Client *c, struct wlr_box geo, int interact);

/* Layout implementations */
void tile(Monitor *m);      /* Traditional master/stack tiling layout */
void monocle(Monitor *m);   /* Single window fullscreen layout */
void infinite(Monitor *m);  /* Infinite scroll layout (like Niri) */

#endif /* LAYOUT_H */
