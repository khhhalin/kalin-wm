#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <wayland-server-core.h>
#include <signal.h>

/* Logging level - defined in main.c */
extern int log_level;

/* Forward declarations */
struct wlr_backend;
struct wlr_renderer;
struct wlr_allocator;
struct wlr_scene;
struct wlr_xdg_shell;
struct wlr_layer_shell_v1;
struct wlr_output_layout;
struct wlr_seat;

/* Global state - defined in compositor.c */
extern struct wl_display *dpy;
extern struct wl_event_loop *event_loop;
extern struct wlr_backend *backend;
extern struct wlr_renderer *drw;
extern struct wlr_allocator *alloc;
extern struct wlr_scene *scene;
extern struct wlr_xdg_shell *xdg_shell;
extern struct wlr_layer_shell_v1 *layer_shell;
extern struct wlr_output_layout *output_layout;

extern struct wlr_seat *seat;

/* Session lock */
extern struct wlr_session_lock_manager_v1 *session_lock_mgr;
extern struct wl_listener new_session_lock;
extern struct wl_list locks;
extern struct wl_list locked_monitors;
extern int locked;
extern struct wlr_scene_rect *locked_bg;

/* Function declarations */

/* Initialize all compositor subsystems */
void setup(void);

/* Run the main event loop */
void run(char *startup_cmd);

/* Cleanup resources */
void cleanup(void);

/* Signal handler */
void handlesig(int sig);

/* Quit the compositor */
void quit(const Arg *arg);

/* Check if compositor is running */
int is_running(void);

#endif /* COMPOSITOR_H */
