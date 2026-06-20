/*
 * Input handling module for kalin-wm
 * Handles keyboard, pointer, and input device events
 */
#ifndef INPUT_H
#define INPUT_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_seat.h>
#include <xkbcommon/xkbcommon.h>

/* Forward declarations */
struct wlr_input_device;
struct wlr_keyboard;
struct wlr_keyboard_group;
struct wlr_pointer;
struct wlr_pointer_motion_event;
struct wlr_pointer_motion_absolute_event;
struct wlr_pointer_button_event;
struct wlr_pointer_axis_event;
struct wlr_keyboard_key_event;
struct wlr_virtual_keyboard_v1;
struct wlr_virtual_pointer_v1_new_pointer_event;

/* Client types - defined in main dwl.c */
typedef struct Client Client;
typedef struct LayerSurface LayerSurface;
typedef struct KeyboardGroup KeyboardGroup;

/* Pointer constraint wrapper */
typedef struct {
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
} PointerConstraint;

/* Input-related globals - defined in main dwl.c */
extern struct wlr_seat *seat;
extern struct wlr_cursor *cursor;
extern struct wlr_xcursor_manager *cursor_mgr;
extern struct wlr_pointer_constraints_v1 *pointer_constraints;
extern struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;
extern struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;
extern struct wlr_virtual_pointer_manager_v1 *virtual_pointer_mgr;
extern struct wlr_idle_notifier_v1 *idle_notifier;

/* Keyboard group */
extern KeyboardGroup *kb_group;

/* Cursor constraint state */
extern struct wlr_pointer_constraint_v1 *active_constraint;

/* Cursor mode state */
extern unsigned int cursor_mode;
extern Client *grabc;
extern int grabcx, grabcy;

/* Lock state (needed for input handling) */
extern int locked;

/* Scene layers (for drag icon) */
extern struct wlr_scene_tree *drag_icon;

/* Drag state */
extern struct wlr_drag *drag;

/* Focus management */
typedef struct {
	unsigned int mod;
	xkb_keysym_t keysym;
	void (*func)(const void *);
	const void *arg;
} Key;

typedef struct {
	unsigned int mod;
	unsigned int button;
	void (*func)(const void *);
	const void *arg;
} Button;

/* Input event listeners (to be registered in setup) */
extern struct wl_listener cursor_axis;
extern struct wl_listener cursor_button;
extern struct wl_listener cursor_frame;
extern struct wl_listener cursor_motion;
extern struct wl_listener cursor_motion_absolute;
extern struct wl_listener new_input_device;
extern struct wl_listener new_virtual_keyboard;
extern struct wl_listener new_virtual_pointer;
extern struct wl_listener new_pointer_constraint;

/* Pointer event handlers */
void axisnotify(struct wl_listener *listener, void *data);
void buttonpress(struct wl_listener *listener, void *data);
void cursorframe(struct wl_listener *listener, void *data);
void motionrelative(struct wl_listener *listener, void *data);
void motionabsolute(struct wl_listener *listener, void *data);
void motionnotify(uint32_t time, struct wlr_input_device *device, double dx, double dy,
		double dx_unaccel, double dy_unaccel);

/* Pointer focus and constraints */
void pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy,
		uint32_t time);
void createpointerconstraint(struct wl_listener *listener, void *data);
void destroypointerconstraint(struct wl_listener *listener, void *data);
void cursorconstrain(struct wlr_pointer_constraint_v1 *constraint);
void cursorwarptohint(void);

/* Keyboard event handlers */
void keypress(struct wl_listener *listener, void *data);
void keypressmod(struct wl_listener *listener, void *data);
int keyrepeat(void *data);
int keybinding(uint32_t mods, xkb_keysym_t sym);

/* Keyboard group management */
KeyboardGroup *createkeyboardgroup(void);
void destroykeyboardgroup(struct wl_listener *listener, void *data);
void createkeyboard(struct wlr_keyboard *keyboard);

/* Input device management */
void inputdevice(struct wl_listener *listener, void *data);
void createpointer(struct wlr_pointer *pointer);

/* Virtual input devices */
void virtualkeyboard(struct wl_listener *listener, void *data);
void virtualpointer(struct wl_listener *listener, void *data);

/* Helper for coordinate conversion and hit testing */
void xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny);

/* Monitor from coordinates */
struct Monitor *xytomon(double x, double y);

/* Client focus (needed by input) */
void focusclient(Client *c, int lift);
Client *focustop(struct Monitor *m);

/* Utility macros from dwl.c */
#ifndef CLEANMASK
#define CLEANMASK(mask) (mask & ~WLR_MODIFIER_CAPS)
#endif

#ifndef LENGTH
#define LENGTH(X) (sizeof X / sizeof X[0])
#endif

#ifndef END
#define END(A) ((A) + LENGTH(A))
#endif

/* External configuration arrays */
extern const Key keys[];
extern const Button buttons[];
extern int sloppyfocus;

#endif /* INPUT_H */
