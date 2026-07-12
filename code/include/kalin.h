/*
 * kalin.h - Main header file for kalin-wm
 *
 * kalin-wm is a Wayland compositor based on dwl with infinite viewport,
 * crop functionality, and Niri-like 2D scrolling.
 */

#ifndef KALIN_H
#define KALIN_H

/* Standard includes */
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

/* Wayland and wlroots includes */
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <xkbcommon/xkbcommon.h>
#include <libinput.h>
#include <linux/input-event-codes.h>

/* Project utility header */
#include "util.h"
#include "crash_report.h"
#include "defensive.h"
#include "persistence.h"

/* Safe mode flag for recovery from repeated crashes */
extern int safe_mode_enabled;
extern struct wl_list clients;

/* ============================================================================
 * Macros
 * ============================================================================ */

#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define CLEANMASK(mask)         (mask & ~WLR_MODIFIER_CAPS)
/* One infinite canvas per monitor: a client is "on" a monitor iff assigned to it. */
#define VISIBLEON(C, M)         ((C) && (M) && (C)->mon == (M) && !(C)->minimized)
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define END(A)                  ((A) + LENGTH(A))
#define LISTEN(E, L, H)         wl_signal_add((E), ((L)->notify = (H), (L)))

#define LISTEN_STATIC(E, H)     listen_static((E), (H))

/* Note: the world<->screen coordinate macros and the viewport/crop-editor state
 * are dwl.c-private (they need the full 10-field camera struct + zoom); they are
 * intentionally NOT declared here. */

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct Monitor Monitor;
typedef struct Client Client;
typedef struct LayerSurface LayerSurface;
typedef struct KeyboardGroup KeyboardGroup;
typedef struct PointerConstraint PointerConstraint;
typedef struct SessionLock SessionLock;

/* ============================================================================
 * Enums
 * ============================================================================ */

/**
 * Cursor modes - determines how the cursor interacts with windows
 */
enum {
    CurNormal,      /* Normal cursor operation */
    CurPressed,     /* Mouse button is pressed */
    CurMove,        /* Moving a window */
    CurResize,      /* Resizing a window */
    CurPan,         /* Dragging the camera (Super+Ctrl+LMB on background) */
    CurCut          /* Super+LMB pressed on empty canvas: sweeping the cursor
                      * near/across a connection line severs it (see
                      * connection_click_hit(), buttonpress()/motionnotify()) */
};

/**
 * Client types - identifies the shell protocol used by a client
 */
enum {
    XDGShell,       /* Standard XDG toplevel */
    LayerShell      /* Layer shell surface */
};

/**
 * Scene layers - z-ordering of different surface types
 */
enum {
    LyrBg,          /* Background layer (wallpaper) */
    LyrBottom,      /* Bottom layer shell surfaces */
    LyrFloat,       /* Every normal window (there's no tiled/floating split
                     * any more — every window is free-positioned). */
    LyrFloatTop,    /* Pinned "always on top" windows: above normal windows,
                     * still below layer-shell LyrTop (bars). */
    LyrTop,         /* Top layer shell surfaces */
    LyrFS,          /* Fullscreen windows */
    LyrOverlay,     /* Overlay layer shell surfaces */
    LyrBlock,       /* Session lock blocking layer */
    NUM_LAYERS      /* Total number of layers */
};

/**
 * Directional navigation directions
 */
enum Direction {
    DIR_LEFT,
    DIR_RIGHT,
    DIR_UP,
    DIR_DOWN
};

/**
 * Octant - the 8 compass directions a Client.neighbor[] connection slot can
 * occupy, assigned by the angle between two windows' centers. Ordered so the
 * opposite direction is always +4 (mod 8): OCT_N<->OCT_S, OCT_E<->OCT_W, etc.
 */
enum Octant {
    OCT_N,
    OCT_NE,
    OCT_E,
    OCT_SE,
    OCT_S,
    OCT_SW,
    OCT_W,
    OCT_NW
};

/* ============================================================================
 * Core Types
 * ============================================================================ */

/**
 * Argument type for key/button bindings - supports int, uint, float, or pointer
 */
#ifndef KALIN_ARG_DEFINED
#define KALIN_ARG_DEFINED
typedef union {
    int i;
    uint32_t ui;
    float f;
    const void *v;
} Arg;
#endif

/**
 * Monitor rule - configuration for new outputs
 */
typedef struct {
    const char *name;           /* Output name pattern to match */
    float scale;                /* Output scale */
    enum wl_output_transform rr;/* Rotation/reflect transform */
    int x, y;                   /* Position (-1, -1 for auto) */
} MonitorRule;

/**
 * Window rule - configuration applied to matching clients
 */
typedef struct {
    const char *id;             /* App ID pattern to match */
    const char *title;          /* Title pattern to match */
    int monitor;                /* Preferred monitor (-1 for current) */
} Rule;

/**
 * Crop state - stores normalized crop rectangle for a client
 */
typedef struct {
    bool active;                /* Whether crop is active */
    float x, y;                 /* Normalized crop position [0-1] */
    float w, h;                 /* Normalized crop size [0-1] */
    int base_w, base_h;         /* Uncropped/base window size */
    bool saved_base;            /* True if base size is captured */

    /* Cache of the last clip box applied via wlr_scene_subsurface_tree_set_clip()
     * (client_apply_crop_clip() runs every frame; skip the call when nothing
     * changed instead of re-issuing an identical clip every frame). */
    int last_clip_x, last_clip_y, last_clip_w, last_clip_h;
    bool clip_cached;
} CropState;

/**
 * Client - represents a window/toplevel surface
 */
struct Client {
    /* Must keep this field first - identifies the client type */
    unsigned int type;          /* XDGShell or LayerShell */

    /* Crop state for window cropping functionality */
    CropState crop;             /* Normalized crop rectangle */

    /* Connection graph: up to 8 neighbor links, one per compass octant (see
     * enum Octant below), assigned by the angle between two windows' centers
     * when the connection forms. Symmetric — if neighbor[DIR_W] == other,
     * then other->neighbor[DIR_E] == this client. NULL slot = no connection
     * in that direction. Used for (a) initial spawn placement (new window
     * connects W/E to whichever window was focused when it was created) and
     * (b) group-drag (dragging any window in a connected component moves the
     * whole component together) and (c) Super+Ctrl+Arrow swap-with-neighbor. */
    Client *neighbor[8];
    uint32_t id;                 /* stable id, for IPC references */

    Monitor *mon;               /* Associated monitor */
    struct wlr_scene_tree *scene;           /* Scene tree node */
    struct wlr_scene_rect *border[4];       /* Border rectangles: top, bottom, left, right */
    struct wlr_scene_rect *focus_ring[4];   /* Focus ring rectangles: top, bottom, left, right */
    struct wlr_scene_tree *scene_surface;   /* Surface scene tree */
    struct wl_list link;                    /* Tiling order link */
    struct wl_list flink;                   /* Focus order link */
    struct wlr_box geom;                    /* Layout-relative geometry, includes border */
    struct wlr_box prev;                    /* Previous geometry, includes border */
    struct wlr_box bounds;                  /* Size bounds (width/height only) */
    union {
        struct wlr_xdg_surface *xdg;
    } surface;                              /* Shell-specific surface */
    struct wlr_xdg_toplevel_decoration_v1 *decoration;
    
    /* Event listeners */
    struct wl_listener commit;
    struct wl_listener map;
    struct wl_listener maximize;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener set_title;
    struct wl_listener fullscreen;
    struct wl_listener set_decoration_mode;
    struct wl_listener destroy_decoration;

    /* wlr-foreign-toplevel-management: lets external shells (e.g. quickshell
     * ToplevelManager) enumerate and control this window. */
    struct wlr_foreign_toplevel_handle_v1 *foreign_toplevel;
    struct wl_listener foreign_activate;
    struct wl_listener foreign_close;
    struct wl_listener foreign_fullscreen;
    struct wl_listener foreign_minimize;

    /* State */
    unsigned int bw;            /* Border width in pixels */
    int isontop;                /* "always on top" pin: stays above other
                                  * windows regardless of subsequent focus
                                  * elsewhere. */
    int allow_overlap;          /* When set, resolve_growth_overlap() skips
                                  * this client entirely: its growth never
                                  * pushes connection-graph neighbors out of
                                  * the way, so it's free to overlap them. */
    int isurgent;               /* Urgency hint */
    int isfullscreen;           /* Fullscreen state */
    int ismaximized;            /* Maximize-toggle (Super+f): fills mon->w, keeps
                                  * border/bar/decorations, unlike isfullscreen. */
    struct wlr_box premax;      /* Geometry before maximizing, for restore */
    int minimized;              /* Hidden from scene/tiling, process stays alive */
    int docked;                 /* Pinned into a shell-panel-owned screen rect
                                  * (see setdocked()): borderless, exempt from
                                  * the world/camera transform like fullscreen,
                                  * geometry driven by IPC "dock" instead of the
                                  * user. Transient — false during the brief
                                  * undock-then-minimize window a panel passes
                                  * through on close (see DockedPanel.qml's
                                  * _close()), so it alone isn't a reliable
                                  * "this is a panel, not a real window" check;
                                  * see ispanel below for that. */
    int ispanel;                 /* Set once, forever, the first time this
                                  * client is ever docked (setdocked()) —
                                  * unlike `docked` above, never cleared by an
                                  * undock. The compositor-wide tag for "this
                                  * is shell-panel chrome, not a real user
                                  * window": never gets a foreign-toplevel
                                  * handle (so it can't appear on a taskbar
                                  * built from that protocol), never
                                  * participates in camera-follow/fit-all/
                                  * overview, connection graph, or directional
                                  * focus. Checked instead of `docked` by
                                  * anything that needs to stay correct across
                                  * that transient undock window too. */
    float opacity;              /* Per-window opacity, 0.1..1.0 */
    uint32_t resize;            /* Configure serial of pending resize */
    int persist_size_pending;   /* Set when persistence_register_client()
                                  * restores a saved width/height on a
                                  * brand-new client — a freshly-mapped
                                  * client's own first non-initial commit
                                  * (finalizing whatever size it natively
                                  * chose) races with that restore and would
                                  * otherwise silently overwrite it via
                                  * commitnotify()'s client_accept_requested_
                                  * size(); consumed (skipping exactly that
                                  * one commit's accept) the first time
                                  * commitnotify() sees it set. */

    /* Spring-glide animation: the layout writes target_geom; a per-frame tick
     * springs the rendered world position (anim_x/anim_y, with velocity vx/vy)
     * toward it so columns slide instead of snapping. Size is applied instantly.
     * anim_ready gates the first placement so a new window doesn't fly in from
     * the origin. */
    struct wlr_box target_geom;
    float anim_x, anim_y;
    float vx, vy;
    int animating;
    int anim_ready;
};

/**
 * LayerSurface - represents a layer-shell surface (panels, wallpapers, etc.)
 */
struct LayerSurface {
    /* Must keep this field first */
    unsigned int type;          /* LayerShell */

    Monitor *mon;               /* Associated monitor */
    struct wlr_scene_tree *scene;           /* Main scene tree */
    struct wlr_scene_tree *popups;          /* Popups scene tree */
    struct wlr_scene_layer_surface_v1 *scene_layer;
    struct wl_list link;                    /* Monitor layer list link */
    int mapped;                             /* Mapped state */
    struct wlr_layer_surface_v1 *layer_surface;

    /* Event listeners */
    struct wl_listener destroy;
    struct wl_listener unmap;
    struct wl_listener surface_commit;
};

/**
 * KeyboardGroup - unified keyboard input handling
 */
struct KeyboardGroup {
    struct wlr_keyboard_group *wlr_group;

    int nsyms;
    xkb_keysym_t *keysyms;          /* Owned copy, invalid if nsyms == 0 */
    uint32_t mods;                  /* Invalid if nsyms == 0 */
    struct wl_event_source *key_repeat_source;

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

/**
 * Monitor - represents an output/display
 */
struct Monitor {
    struct wl_list link;                    /* Monitor list link */
    struct wlr_output *wlr_output;          /* wlroots output handle */
    struct wlr_scene_output *scene_output;  /* Scene output handle */
    struct wlr_scene_rect *fullscreen_bg;   /* Fullscreen background rect */
    struct wl_listener frame;               /* Frame event listener */
    struct wl_listener destroy;             /* Destroy event listener */
    struct wl_listener request_state;       /* State request listener */
    struct wl_listener destroy_lock_surface;/* Lock surface destroy listener */
    struct wlr_session_lock_surface_v1 *lock_surface;
    struct wlr_box m;                       /* Monitor area, layout-relative */
    struct wlr_box w;                       /* Window area, layout-relative */
    struct wl_list layers[4];               /* LayerSurface lists by layer */
    int gamma_lut_changed;                  /* Gamma LUT needs update */
    int asleep;                             /* Power management state */
    int arrange_dirty;                      /* Needs arrange() before next idle flush
                                              * (see modules/layout/arrange_sched.c) */
};

/**
 * PointerConstraint - pointer lock/confine for games/applications
 */
struct PointerConstraint {
    struct wlr_pointer_constraint_v1 *constraint;
    struct wl_listener destroy;
};

/**
 * SessionLock - screen lock state
 */
struct SessionLock {
    struct wlr_scene_tree *scene;
    struct wlr_session_lock_v1 *lock;
    struct wl_listener new_surface;
    struct wl_listener unlock;
    struct wl_listener destroy;
};

/**
 * Viewport - global 2D view transform (camera) over the infinite canvas.
 * dwl.c owns the single instance; viewport/layout/crop/ipc TUs read it.
 */
typedef struct {
    float x, y;                 /* camera position */
    float target_x, target_y;   /* camera animation target */
    float zoom;                 /* zoom level (1.0 = normal) */
    float target_zoom;          /* zoom animation target */
    int follow;                 /* 1 = camera follows focused window */
    int follow_new_windows;     /* 1 = auto-pan to new windows */
    int smooth_pan;             /* 1 = animate camera movement */
    int animating;              /* 1 = moving toward target, or coasting (below) */
    int coasting;                /* 1 = decelerating from a trackpad flick, not
                                   * easing toward target_x/y (see viewport_tick()
                                   * and viewport_coast_start(), viewport_ops.c) */
    float vel_x, vel_y;          /* world-units/sec camera velocity while coasting */
} Viewport;

/* World<->screen coordinate transforms (pan + zoom), and the minimum gap the
 * connection-graph spawn/gap-closing logic keeps between edge-connected
 * windows. Placed here, above the DWL_INTERNAL split below, because both
 * dwl.c (which defines `viewport` directly) and every separately-compiled
 * module (which sees it via the `extern Viewport viewport` below) need
 * these — dwl.c specifically does NOT see anything past the "#ifndef
 * DWL_INTERNAL" guard below, since it defines those symbols itself. */
#define VIEWPORT_ZOOM_SAFE    (viewport.zoom > 0.0001f ? viewport.zoom : 0.0001f)
#define WORLD_TO_SCREEN_X(wx) ((int)(((wx) - viewport.x) * VIEWPORT_ZOOM_SAFE))
#define WORLD_TO_SCREEN_Y(wy) ((int)(((wy) - viewport.y) * VIEWPORT_ZOOM_SAFE))
#define SCREEN_TO_WORLD_X(sx) ((float)(sx) / VIEWPORT_ZOOM_SAFE + viewport.x)
#define SCREEN_TO_WORLD_Y(sy) ((float)(sy) / VIEWPORT_ZOOM_SAFE + viewport.y)
#define SPAWN_GAP 20

/**
 * Wallpaper - stationary tiled background scene state (owned by dwl.c).
 */
typedef struct {
    struct wlr_scene_tree *tree;
    struct wlr_scene_tree **tiles;
    int tiles_x;
    int tiles_y;
    int tile_size;
    int configured_w;
    int configured_h;
} Wallpaper;

/**
 * CropEditor - transient crop-mode UI state (owned by dwl.c).
 */
typedef struct {
    bool active;
    Client *target;
    double start_x, start_y;
    double end_x, end_y;
    bool dragging;
    struct wlr_scene_rect *overlay;      /* dark fullscreen overlay */
    struct wlr_scene_rect *border[4];    /* border lines: top, bottom, left, right */
    struct wlr_scene_rect *handles[4];   /* corner handles: TL, TR, BL, BR */
    struct wlr_scene_rect *crosshair_h;  /* horizontal center line */
    struct wlr_scene_rect *crosshair_v;  /* vertical center line */
} CropEditor;

/**
 * ScreenshotEditor - transient niri-style screenshot-UI state (owned by
 * dwl.c). Selection coordinates are in the same screen-pixel space as
 * CropEditor's (cursor->x/y, matching Monitor.m). Opens with the whole
 * monitor pre-selected; dragging draws a custom rectangle over it.
 */
typedef struct {
    bool active;
    Monitor *mon;
    double start_x, start_y;
    double end_x, end_y;
    bool dragging;
    bool show_pointer;
    struct wlr_scene_rect *overlay;      /* dark fullscreen overlay */
    struct wlr_scene_rect *border[4];    /* border lines: top, bottom, left, right */
} ScreenshotEditor;

/* Everything below is the runtime interface (globals + prototypes) that the
 * separately-compiled TUs link against. dwl.c OWNS these symbols (defines them
 * as file-scope statics), so it includes this header with DWL_INTERNAL defined
 * to pull in ONLY the shared types above and skip the clashing declarations. */
#ifndef DWL_INTERNAL

/* ============================================================================
 * Global Variables (extern declarations)
 * ============================================================================ */

/* Process and state */
extern pid_t child_pid;
extern int locked;
extern void *exclusive_focus;

/* Wayland core */
extern struct wl_display *dpy;
extern struct wl_event_loop *event_loop;
extern struct wlr_backend *backend;

/* Scene graph */
extern struct wlr_scene *scene;
extern struct wlr_scene_tree *layers[NUM_LAYERS];
extern struct wlr_scene_tree *drag_icon;
extern const int layermap[];

/* Rendering */
extern struct wlr_renderer *drw;
extern struct wlr_allocator *alloc;
extern struct wlr_compositor *compositor;
extern struct wlr_session *session;

/* Shell protocols */
extern struct wlr_xdg_shell *xdg_shell;
extern struct wlr_xdg_activation_v1 *activation;
extern struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_mgr;
extern struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
extern struct wl_list clients;      /* Tiling order */
extern struct wl_list fstack;       /* Focus order */

/* Idle and power management */
extern struct wlr_idle_notifier_v1 *idle_notifier;
extern struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
extern struct wlr_layer_shell_v1 *layer_shell;
extern struct wlr_output_manager_v1 *output_mgr;
extern struct wlr_output_power_manager_v1 *power_mgr;
extern struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;
extern struct wlr_virtual_pointer_manager_v1 *virtual_pointer_mgr;
extern struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr;

/* Pointer constraints */
extern struct wlr_pointer_constraints_v1 *pointer_constraints;
extern struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;
extern struct wlr_pointer_constraint_v1 *active_constraint;

/* Cursor */
extern struct wlr_cursor *cursor;
extern struct wlr_xcursor_manager *cursor_mgr;

/* Background */
extern struct wlr_scene_rect *root_bg;

/* Seat and input */
extern struct wlr_seat *seat;
extern KeyboardGroup *kb_group;
extern unsigned int cursor_mode;
extern Client *grabc;
extern int grabcx, grabcy;          /* Client-relative grab offsets */

/* Output layout */
extern struct wlr_output_layout *output_layout;
extern struct wlr_box sgeom;        /* Screen geometry */
extern struct wl_list mons;         /* Monitor list */
extern Monitor *selmon;             /* Selected monitor */

/* Wallpaper colors */
extern const float wallbg_rgba[4];
extern const float wallpattern_rgba[4];

/* Shared runtime state promoted from dwl.c file scope so the separately-
 * compiled feature TUs can link against it (see the typedefs above). */
extern Viewport viewport;
extern Wallpaper wallpaper;
extern CropEditor crop_editor;
extern ScreenshotEditor screenshot_ui;
extern Client *dock_hover_client;

/* State bits the ipc TU mirrors to the shell (hold-Super overlay + exit prompt). */
extern int super_held;
extern int exit_pending;
extern int menu_shown;

/* Event listeners */
extern struct wl_listener cursor_axis;
extern struct wl_listener cursor_button;
extern struct wl_listener cursor_frame;
extern struct wl_listener cursor_motion;
extern struct wl_listener cursor_motion_absolute;
extern struct wl_listener gpu_reset;
extern struct wl_listener layout_change;
extern struct wl_listener new_idle_inhibitor;
extern struct wl_listener new_input_device;
extern struct wl_listener new_virtual_keyboard;
extern struct wl_listener new_virtual_pointer;
extern struct wl_listener new_pointer_constraint;
extern struct wl_listener new_output;
extern struct wl_listener new_xdg_toplevel;
extern struct wl_listener new_xdg_popup;
extern struct wl_listener new_xdg_decoration;
extern struct wl_listener new_layer_surface;
extern struct wl_listener output_mgr_apply;
extern struct wl_listener output_mgr_test;
extern struct wl_listener output_power_mgr_set_mode;
extern struct wl_listener request_activate;
extern struct wl_listener request_cursor;
extern struct wl_listener request_set_psel;
extern struct wl_listener request_set_sel;
extern struct wl_listener request_set_cursor_shape;
extern struct wl_listener request_start_drag;
extern struct wl_listener start_drag;

/* ============================================================================
 * Function Declarations - Core
 * ============================================================================ */

/* Client management */
void applybounds(Client *c, struct wlr_box *bbox);
void applyrules(Client *c);
void arrange(Monitor *m);
void viewport_camera_tick(Monitor *m);

/* Arrangement scheduler (modules/layout/arrange_sched.c): coalesces any number of
 * arrange()/printstatus()-worthy mutations within one event-loop iteration into a
 * single real arrange() + printstatus() call, via a deferred idle callback. Call
 * sites that used to call arrange(m)/printstatus() directly should call these
 * instead. */
void arrange_mark_dirty(Monitor *m);
void status_mark_dirty(void);
void arrangelayer(Monitor *m, struct wl_list *list,
        struct wlr_box *usable_area, int exclusive);
void arrangelayers(Monitor *m);
void closemon(Monitor *m);
void commitnotify(struct wl_listener *listener, void *data);
void commitpopup(struct wl_listener *listener, void *data);
void createdecoration(struct wl_listener *listener, void *data);
void createnotify(struct wl_listener *listener, void *data);
void createpopup(struct wl_listener *listener, void *data);
void destroydecoration(struct wl_listener *listener, void *data);
void destroydragicon(struct wl_listener *listener, void *data);
void destroynotify(struct wl_listener *listener, void *data);
void focusclient(Client *c, int lift);
void focusstack(const Arg *arg);
Client *focustop(Monitor *m);
void fullscreennotify(struct wl_listener *listener, void *data);
void killclient(const Arg *arg);
void mapnotify(struct wl_listener *listener, void *data);
void maximizenotify(struct wl_listener *listener, void *data);
void moveresize(const Arg *arg);
void pointerfocus(Client *c, struct wlr_surface *surface,
        double sx, double sy, uint32_t time);
void resize(Client *c, struct wlr_box geo, int interact);
void resizefocused(const Arg *arg);
void fitwidth(const Arg *arg);
void fitheight(const Arg *arg);
void setfullscreen(Client *c, int fullscreen);
void setmaximized(Client *c, int maximized);
void setminimized(Client *c, int minimized);
void setdocked(Client *c, int docked, struct wlr_box rect);
Client *client_find_by_appid(const char *appid);
void dockprep_register(const char *appid, struct wlr_box rect);
int dockprep_consume(const char *appid, struct wlr_box *out);
Monitor *monitor_find_by_name(const char *name);
int ipc_set_output(const char *name, int width, int height, float refresh,
        float scale, int x, int y, int enabled);
int backlight_get(int *value, int *max);
int backlight_set(int value);
void setopacity(Client *c, float opacity);
void opacityadjust(const Arg *arg);
void setmon(Client *c, Monitor *m);
void tagmon(const Arg *arg);
void togglefullscreen(const Arg *arg);
void togglemaximized(const Arg *arg);
void toggleontop(const Arg *arg);
void toggleoverlap(const Arg *arg);
void sever_connection(uint32_t id_a, uint32_t id_b);
void connect_clients(Client *a, Client *b);
void resolve_growth_overlap(Client *c);
void connect_pick_arm(void);
void connect_pick_cancel(void);
void connect_pick_complete(Client *target);
Client *connect_pick_pending(void);
void toggleminimize(const Arg *arg);
void togglescratchpad(const Arg *arg);
void unmapnotify(struct wl_listener *listener, void *data);
void updatetitle(struct wl_listener *listener, void *data);
void urgent(struct wl_listener *listener, void *data);

/* Monitor management */
void cleanupmon(struct wl_listener *listener, void *data);
void createmon(struct wl_listener *listener, void *data);
Monitor *dirtomon(enum wlr_direction dir);
void focusmon(const Arg *arg);
void powermgrsetmode(struct wl_listener *listener, void *data);
void rendermon(struct wl_listener *listener, void *data);
void requestmonstate(struct wl_listener *listener, void *data);
void updatemons(struct wl_listener *listener, void *data);
Monitor *xytomon(double x, double y);
void xytonode(double x, double y, struct wlr_surface **psurface,
        Client **pc, LayerSurface **pl, double *nx, double *ny);

/* Layer shell */
void commitlayersurfacenotify(struct wl_listener *listener, void *data);
void createlayersurface(struct wl_listener *listener, void *data);
void destroylayersurfacenotify(struct wl_listener *listener, void *data);
void unmaplayersurfacenotify(struct wl_listener *listener, void *data);

/* Keyboard and input */
void chvt(const Arg *arg);
void createkeyboard(struct wlr_keyboard *keyboard);
KeyboardGroup *createkeyboardgroup(void);
void destroykeyboardgroup(struct wl_listener *listener, void *data);
void inputdevice(struct wl_listener *listener, void *data);
int keybinding(uint32_t mods, xkb_keysym_t sym);
int keybinding_repeatable(void);
void keypress(struct wl_listener *listener, void *data);
void keypressmod(struct wl_listener *listener, void *data);
int keyrepeat(void *data);
void virtualkeyboard(struct wl_listener *listener, void *data);

/* Pointer and cursor */
void axisnotify(struct wl_listener *listener, void *data);
void buttonpress(struct wl_listener *listener, void *data);
void createpointer(struct wlr_pointer *pointer);
void createpointerconstraint(struct wl_listener *listener, void *data);
void cursorconstrain(struct wlr_pointer_constraint_v1 *constraint);
void cursorframe(struct wl_listener *listener, void *data);
void cursorwarptohint(void);
void destroypointerconstraint(struct wl_listener *listener, void *data);
void motionabsolute(struct wl_listener *listener, void *data);
void motionnotify(uint32_t time, struct wlr_input_device *device, double sx,
        double sy, double sx_unaccel, double sy_unaccel);
void motionrelative(struct wl_listener *listener, void *data);
void requeststartdrag(struct wl_listener *listener, void *data);
void setcursor(struct wl_listener *listener, void *data);
void setcursorshape(struct wl_listener *listener, void *data);
void startdrag(struct wl_listener *listener, void *data);
void virtualpointer(struct wl_listener *listener, void *data);

/* Output management */
void outputmgrapply(struct wl_listener *listener, void *data);
void outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test);
void outputmgrtest(struct wl_listener *listener, void *data);

/* Session lock (modules/session_lock.c) */
void destroylocksurface(struct wl_listener *listener, void *data);
void session_lock_init(void);
void session_lock_resize(void);
void session_lock_configure_output(Monitor *m);
void session_lock_cleanup(void);

/* Viewport and navigation (kalin-wm specific) */
void viewport_kick(void);
void viewport_pan(const Arg *arg);
void viewport_coast_start(float vx, float vy);
void viewport_fit_all(const Arg *arg);
void viewport_zoom(const Arg *arg);
void viewport_reset(const Arg *arg);
void viewport_center_on(Client *c);
void viewport_center_on_x(Client *c);
void viewport_center_on_y(Client *c);
void viewport_menu_reveal(Client *c);
void viewport_focus_window(Client *c);
void viewport_animate_to(float x, float y, float zoom);
void viewport_toggle_follow(const Arg *arg);
void viewport_toggle_follow_new(const Arg *arg);
void viewport_follow_focus(void);
void viewport_pan_grab_start(void);
void viewport_pan_grab_update(void);

/* Overview (modules/viewport/overview.c): Super+O zooms out to frame every
 * window (reuses viewport_fit_all()'s shot) and remembers the camera so it
 * can snap back on toggle, click-to-focus, or bare Escape. */
void toggle_overview(const Arg *arg);
void overview_exit(void);
void overview_select(Client *c);
int overview_is_active(void);

/* hyprland-toplevel-export-v1 (modules/protocols/toplevel_export.c): per-window
 * frame capture for Quickshell's taskbar hover-preview and Overview thumbnails —
 * see that file's header comment for why this specific (non-wlroots-wrapped)
 * protocol is required. */
void toplevel_export_init(struct wl_display *display);

/* Hold-Super spotlight: focus the active window and recede the rest (defined in
 * dwl.c, driven by the shell via the "spotlight" IPC command). */
void spotlight_enter(void);
void spotlight_exit(void);

/* High-res screenshot (capture TU) */
void capture_screenshot(const Arg *arg);

/* Region/selection export used by the screenshot UI (capture TU): renders
 * monitor `m` at native resolution, crops to the selection box (in the same
 * screen-pixel space as ScreenshotEditor), and writes to disk
 * (~/Pictures/Screenshots/) and/or the Wayland clipboard (via wl-copy). */
void capture_export_selection(Monitor *m, int sel_x, int sel_y, int sel_w, int sel_h,
                               bool to_disk, bool to_clipboard);

/* Crop mode (crop_mode TU) */
void cropbegin(const Arg *arg);
void cropcancel(const Arg *arg);
void cropreset(const Arg *arg);
void cropend(const Arg *arg);
void cropdraw(void);

/* Niri-style screenshot UI (screenshot_ui TU): Super+Shift+S opens a
 * drag-to-select overlay pre-filled with the whole focused monitor. Escape
 * cancels; Space/Enter confirms (save to disk + clipboard); Ctrl+C confirms
 * clipboard-only; P toggles pointer visibility in the capture. */
void screenshotui_begin(const Arg *arg);
void screenshotui_cancel(const Arg *arg);
void screenshotui_confirm(bool write_to_disk);
void screenshotui_toggle_pointer(void);
void screenshotui_draw(void);

/* Spring-glide animation: layout sets a client's target world geometry and the
 * compositor tick slides it there (defined in dwl.c). */
void client_set_target_geom(Client *c, struct wlr_box geo);

/* Ask clients to render at zoom DPI so zoomed content is crisp; called from
 * viewport_tick() on camera settle (defined in dwl.c). */
void client_apply_zoom_scale(void);

/* Directional focus navigation */
void focus_directional(const Arg *arg);

/* Wallpaper (kalin-wm specific) */
void wallpaper_init(void);

/* Crop functionality (kalin-wm specific) */
void cropbegin(const Arg *arg);
void cropcancel(const Arg *arg);
void cropend(const Arg *arg);
void cropdraw(void);

/* Selection and clipboard */
void setpsel(struct wl_listener *listener, void *data);
void setsel(struct wl_listener *listener, void *data);

/* Shell IPC socket (defined in the separately-compiled modules/ipc.c TU). */
void ipc_broadcast_state(void);

/* System and lifecycle */
void checkidleinhibitor(struct wlr_surface *exclude);
void cleanup(void);
void cleanuplisteners(void);
void gpureset(struct wl_listener *listener, void *data);
void handlesig(int signo);
void printstatus(void);
void quit(const Arg *arg);
void requestdecorationmode(struct wl_listener *listener, void *data);
void run(char *startup_cmd);
void setup(void);
void spawn(const Arg *arg);
/* Notify the PTY subsystem that a child pid was reaped by SIGCHLD handler. */
void pty_child_reaped(pid_t pid);
int pty_inject(pid_t pid, const char *text);
const char *pty_log_for(pid_t pid);

/* Idle inhibitor */
void createidleinhibitor(struct wl_listener *listener, void *data);
void destroyidleinhibitor(struct wl_listener *listener, void *data);

#endif /* !DWL_INTERNAL */

#endif /* KALIN_H */
