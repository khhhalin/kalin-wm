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
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <xkbcommon/xkbcommon.h>
#include <libinput.h>
#include <linux/input-event-codes.h>

#ifdef XWAYLAND
#include <wlr/xwayland.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#endif

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
#define VISIBLEON(C, M)         ((C) && (M) && (C)->mon == (M) && ((C)->tags & (M)->tagset[(M)->seltags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define END(A)                  ((A) + LENGTH(A))
#define TAGMASK                 ((1u << TAGCOUNT) - 1)
#define LISTEN(E, L, H)         wl_signal_add((E), ((L)->notify = (H), (L)))

#define LISTEN_STATIC(E, H)     listen_static((E), (H))

/* 
 * Coordinate conversion macros
 * Zoom only affects buffer scale, not positions.
 * This gives camera-like zoom where zooming out shows more canvas.
 */
#define WORLD_TO_SCREEN_X(wx)   ((int)((wx) - viewport.x))
#define WORLD_TO_SCREEN_Y(wy)   ((int)((wy) - viewport.y))
#define SCREEN_TO_WORLD_X(sx)   ((float)((sx) / viewport.zoom + viewport.x))
#define SCREEN_TO_WORLD_Y(sy)   ((float)((sy) / viewport.zoom + viewport.y))

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
    CurResize       /* Resizing a window */
};

/**
 * Client types - identifies the shell protocol used by a client
 */
enum {
    XDGShell,       /* Standard XDG toplevel */
    LayerShell,     /* Layer shell surface */
    X11             /* XWayland window */
};

/**
 * Scene layers - z-ordering of different surface types
 */
enum {
    LyrBg,          /* Background layer (wallpaper) */
    LyrBottom,      /* Bottom layer shell surfaces */
    LyrTile,        /* Tiled windows */
    LyrFloat,       /* Floating windows */
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

/* ============================================================================
 * Core Types
 * ============================================================================ */

/**
 * Argument type for key/button bindings - supports int, uint, float, or pointer
 */
typedef union {
    int i;
    uint32_t ui;
    float f;
    const void *v;
} Arg;

/**
 * Mouse button binding
 */
typedef struct {
    unsigned int mod;           /* Modifier key mask */
    unsigned int button;        /* Mouse button (BTN_LEFT, etc.) */
    void (*func)(const Arg *);  /* Function to call */
    const Arg arg;              /* Argument to pass */
} Button;

/**
 * Keyboard key binding
 */
typedef struct {
    uint32_t mod;               /* Modifier key mask */
    xkb_keysym_t keysym;        /* Keysym to match */
    void (*func)(const Arg *);  /* Function to call */
    const Arg arg;              /* Argument to pass */
} Key;

/**
 * Layout definition - contains symbol name and arrange function
 */
typedef struct {
    const char *symbol;         /* Display symbol for the layout */
    void (*arrange)(Monitor *); /* Arrangement function */
} Layout;

/**
 * Monitor rule - configuration for new outputs
 */
typedef struct {
    const char *name;           /* Output name pattern to match */
    float mfact;                /* Master factor */
    int nmaster;                /* Number of master windows */
    float scale;                /* Output scale */
    const Layout *lt;           /* Default layout */
    enum wl_output_transform rr;/* Rotation/reflect transform */
    int x, y;                   /* Position (-1, -1 for auto) */
} MonitorRule;

/**
 * Window rule - configuration applied to matching clients
 */
typedef struct {
    const char *id;             /* App ID pattern to match */
    const char *title;          /* Title pattern to match */
    uint32_t tags;              /* Tags to assign */
    int isfloating;             /* Start as floating */
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
} CropState;

/**
 * World coordinates - persistent position in the infinite canvas
 */
typedef struct {
    float x, y;                 /* World position (not screen position) */
    bool set;                   /* True if world position has been assigned */
} WorldCoords;

/**
 * Client - represents a window/toplevel surface
 */
struct Client {
    /* Must keep this field first - identifies the client type */
    unsigned int type;          /* XDGShell, LayerShell, or X11 */
    
    /* Crop state for window cropping functionality */
    CropState crop;             /* Normalized crop rectangle */
    
    /* World coordinates for infinite layout */
    WorldCoords world;          /* Persistent position in canvas */
    
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
        struct wlr_xwayland_surface *xwayland;
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
#ifdef XWAYLAND
    struct wl_listener activate;
    struct wl_listener associate;
    struct wl_listener dissociate;
    struct wl_listener configure;
    struct wl_listener set_hints;
#endif
    
    /* State */
    unsigned int bw;            /* Border width in pixels */
    uint32_t tags;              /* Tag bitfield */
    int isfloating;             /* Floating state */
    int isurgent;               /* Urgency hint */
    int isfullscreen;           /* Fullscreen state */
    uint32_t resize;            /* Configure serial of pending resize */
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
    const Layout *lt[2];                    /* Selected and previous layout */
    unsigned int seltags;                   /* Selected tagset index */
    unsigned int sellt;                     /* Selected layout index */
    uint32_t tagset[2];                     /* Tagsets for view switching */
    float mfact;                            /* Master factor for tiling */
    int gamma_lut_changed;                  /* Gamma LUT needs update */
    int nmaster;                            /* Number of master windows */
    char ltsymbol[16];                      /* Current layout symbol */
    int asleep;                             /* Power management state */
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
 * Viewport state - global 2D view transform for infinite layout
 */
typedef struct {
    float x, y;              /* Camera position in world coordinates */
    float zoom;              /* Zoom level (1.0 = normal) */
    int follow;              /* 1 = camera follows focused window */
    int follow_new_windows;  /* 1 = auto-pan to new windows */
} Viewport;

/**
 * Wallpaper state - stationary background elements
 */
typedef struct {
    struct wlr_scene_rect *bg;              /* Background rectangle */
    struct wlr_scene_rect **lines;          /* Grid lines array */
    int num_lines;                          /* Number of grid lines */
} Wallpaper;

/**
 * CropEditor state - interactive crop selection UI
 */
typedef struct {
    bool active;                /* Editor is active */
    Client *target;             /* Client being cropped */
    double start_x, start_y;    /* Selection start position */
    double end_x, end_y;        /* Selection end position */
    bool dragging;              /* Currently dragging selection */
    struct wlr_scene_rect *overlay;      /* Dark fullscreen overlay */
    struct wlr_scene_rect *selection;    /* Selection rectangle border */
    struct wlr_scene_rect *selection_bg; /* Selection rectangle fill */
} CropEditor;

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

/* Session lock */
extern struct wlr_scene_rect *root_bg;
extern struct wlr_session_lock_manager_v1 *session_lock_mgr;
extern struct wlr_scene_rect *locked_bg;
extern struct wlr_session_lock_v1 *cur_lock;

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

/* Kalin-wm specific state */
extern Viewport viewport;
extern Wallpaper wallpaper;
extern CropEditor crop_editor;

/* Wallpaper colors */
extern const float wallbg_rgba[4];
extern const float wallpattern_rgba[4];

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
extern struct wl_listener new_session_lock;

#ifdef XWAYLAND
extern struct wl_listener new_xwayland_surface;
extern struct wl_listener xwayland_ready;
extern struct wlr_xwayland *xwayland;
#endif

/* ============================================================================
 * Function Declarations - Core
 * ============================================================================ */

/* Client management */
void applybounds(Client *c, struct wlr_box *bbox);
void applyrules(Client *c);
void arrange(Monitor *m);
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
void setfloating(Client *c, int floating);
void setfullscreen(Client *c, int fullscreen);
void setmon(Client *c, Monitor *m, uint32_t newtags);
void tag(const Arg *arg);
void tagmon(const Arg *arg);
void togglefloating(const Arg *arg);
void togglefullscreen(const Arg *arg);
void toggletag(const Arg *arg);
void unmapnotify(struct wl_listener *listener, void *data);
void updatetitle(struct wl_listener *listener, void *data);
void urgent(struct wl_listener *listener, void *data);
void zoom(const Arg *arg);

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

/* Layout functions */
void infinite(Monitor *m);
void monocle(Monitor *m);
void tile(Monitor *m);

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

/* Session lock */
void createlocksurface(struct wl_listener *listener, void *data);
void destroylock(SessionLock *lock, int unlocked);
void destroylocksurface(struct wl_listener *listener, void *data);
void destroysessionlock(struct wl_listener *listener, void *data);
void locksession(struct wl_listener *listener, void *data);
void unlocksession(struct wl_listener *listener, void *data);

/* Viewport and navigation (kalin-wm specific) */
void viewport_pan(const Arg *arg);
void viewport_zoom(const Arg *arg);
void viewport_reset(const Arg *arg);
void viewport_center_on(Client *c);
void viewport_toggle_follow(const Arg *arg);
void viewport_toggle_follow_new(const Arg *arg);
void viewport_follow_focus(void);

/* Column layout helpers */
void arrange_columns(Monitor *m);
void place_window_column(Client *c, Monitor *m);

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

/* Layout and tags */
void setlayout(const Arg *arg);
void setmfact(const Arg *arg);
void view(const Arg *arg);
void incnmaster(const Arg *arg);

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

/* XWayland (optional) */
#ifdef XWAYLAND
void activatex11(struct wl_listener *listener, void *data);
void associatex11(struct wl_listener *listener, void *data);
void configurex11(struct wl_listener *listener, void *data);
void createnotifyx11(struct wl_listener *listener, void *data);
void dissociatex11(struct wl_listener *listener, void *data);
void sethints(struct wl_listener *listener, void *data);
void xwaylandready(struct wl_listener *listener, void *data);
#endif

/* Idle inhibitor */
void createidleinhibitor(struct wl_listener *listener, void *data);
void destroyidleinhibitor(struct wl_listener *listener, void *data);

#endif /* KALIN_H */
