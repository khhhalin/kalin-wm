/*
 * See LICENSE file for copyright and license details.
 */
#include <errno.h>
#include <getopt.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
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
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <xkbcommon/xkbcommon.h>

#include "util.h"
#include "persistence.h"

/* PTY support */
#include <pty.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>

/* Shared data model + macros (MAX/MIN/LENGTH/LISTEN/VISIBLEON/...) live in
 * kalin.h. dwl.c OWNS the runtime globals as file-scope statics, so it defines
 * DWL_INTERNAL to pull in only the types and skip kalin.h's extern block. */
#define DWL_INTERNAL
#include "kalin.h"

/* Crop editor state */
static struct {
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
} crop_editor;

/* Crop mode visuals - bright border on transparent selection */
#define CROP_OVERLAY_ALPHA 0.5f     /* Dark overlay for contrast */
#define CROP_BORDER_BRIGHT 1.0f     /* Full white brightness */
#define CROP_HANDLE_SIZE 12         /* Corner handle size in pixels */
#define CROP_BORDER_WIDTH 2         /* Border line thickness */

/* 2D Viewport state - global view transform */
static struct {
	float x, y;              /* camera position */
	float target_x, target_y;/* camera animation target */
	float zoom;              /* zoom level (1.0 = normal) */
	float target_zoom;       /* zoom animation target */
	int follow;              /* 1 = camera follows focused window */
	int follow_new_windows;  /* 1 = auto-pan to new windows */
	int smooth_pan;          /* 1 = animate camera movement */
	int animating;           /* 1 = moving toward target */
} viewport = { 0, 0, 0, 0, 1.0, 1.0, 1, 1, 1, 0 };

/* True scene-zoom transform: screen = (world - camera) * zoom. Defined here so
 * input handlers, crop, and the layout (#included later) all share it. Window
 * SIZES are scaled by zoom in resize(); positions by these macros. */
#define VIEWPORT_ZOOM_SAFE    (viewport.zoom > 0.0001f ? viewport.zoom : 0.0001f)
#define WORLD_TO_SCREEN_X(wx) ((int)(((wx) - viewport.x) * VIEWPORT_ZOOM_SAFE))
#define WORLD_TO_SCREEN_Y(wy) ((int)(((wy) - viewport.y) * VIEWPORT_ZOOM_SAFE))
#define SCREEN_TO_WORLD_X(sx) ((float)(sx) / VIEWPORT_ZOOM_SAFE + viewport.x)
#define SCREEN_TO_WORLD_Y(sy) ((float)(sy) / VIEWPORT_ZOOM_SAFE + viewport.y)

/* Exit confirmation state */
static struct {
	time_t last_press;       /* time of first exit keypress */
	int pending;             /* 1 = waiting for confirmation */
} exit_confirm = { 0, 0 };

/* Stationary wallpaper background */
static struct {
	struct wlr_scene_tree *tree;
	struct wlr_scene_tree **tiles;
	int tiles_x;
	int tiles_y;
	int tile_size;
	int configured_w;
	int configured_h;
} wallpaper;

#define WINDOW_SIZE_HISTORY_MAX 256
#define WINDOW_SIZE_KEY_MAX 256
typedef struct {
	char key[WINDOW_SIZE_KEY_MAX];
	int width;
	int height;
	unsigned long stamp;
} WindowSizeHistoryEntry;

static struct {
	WindowSizeHistoryEntry entries[WINDOW_SIZE_HISTORY_MAX];
	unsigned long stamp;
} window_size_history;


/* function declarations */
static void applybounds(Client *c, struct wlr_box *bbox);
static void applyrules(Client *c);
void arrange(Monitor *m);
static void arrangelayer(Monitor *m, struct wl_list *list,
		struct wlr_box *usable_area, int exclusive);
static void arrangelayers(Monitor *m);
static void axisnotify(struct wl_listener *listener, void *data);
static void buttonpress(struct wl_listener *listener, void *data);
static void chvt(const Arg *arg);
static void checkidleinhibitor(struct wlr_surface *exclude);
static void cleanup(void);
static void cleanupmon(struct wl_listener *listener, void *data);
static void cleanuplisteners(void);
static void closemon(Monitor *m);
static void commitlayersurfacenotify(struct wl_listener *listener, void *data);
static void commitnotify(struct wl_listener *listener, void *data);
int client_accept_requested_size(Client *c);
static void commitpopup(struct wl_listener *listener, void *data);
static void createdecoration(struct wl_listener *listener, void *data);
static void createidleinhibitor(struct wl_listener *listener, void *data);
static void createkeyboard(struct wlr_keyboard *keyboard);
static KeyboardGroup *createkeyboardgroup(void);
static void createlayersurface(struct wl_listener *listener, void *data);
static void createlocksurface(struct wl_listener *listener, void *data);
static void createmon(struct wl_listener *listener, void *data);
static void createnotify(struct wl_listener *listener, void *data);
static void createpointer(struct wlr_pointer *pointer);
static void createpointerconstraint(struct wl_listener *listener, void *data);
static void createpopup(struct wl_listener *listener, void *data);
static void cursorconstrain(struct wlr_pointer_constraint_v1 *constraint);
static void cursorframe(struct wl_listener *listener, void *data);
static void cursorwarptohint(void);
static void destroydecoration(struct wl_listener *listener, void *data);
static void destroydragicon(struct wl_listener *listener, void *data);
static void destroyidleinhibitor(struct wl_listener *listener, void *data);
static void destroylayersurfacenotify(struct wl_listener *listener, void *data);
static void destroylock(SessionLock *lock, int unlocked);
static void destroylocksurface(struct wl_listener *listener, void *data);
static void destroynotify(struct wl_listener *listener, void *data);
static void destroypointerconstraint(struct wl_listener *listener, void *data);
static void destroysessionlock(struct wl_listener *listener, void *data);
static void destroykeyboardgroup(struct wl_listener *listener, void *data);
static Monitor *dirtomon(enum wlr_direction dir);
void focusclient(Client *c, int lift);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
Client *focustop(Monitor *m);
static void focus_top(Monitor *m, int lift);
static void fullscreennotify(struct wl_listener *listener, void *data);
static void gpureset(struct wl_listener *listener, void *data);
static void handlesig(int signo);
static void inputdevice(struct wl_listener *listener, void *data);
static int keybinding(uint32_t mods, xkb_keysym_t sym);
static void keypress(struct wl_listener *listener, void *data);
static void keypressmod(struct wl_listener *listener, void *data);
static int keyrepeat(void *data);
static void killclient(const Arg *arg);
static void locksession(struct wl_listener *listener, void *data);
static void mapnotify(struct wl_listener *listener, void *data);
static void maximizenotify(struct wl_listener *listener, void *data);
static int window_size_history_make_key(Client *c, char *buf, size_t buflen);
static int window_size_history_load(Client *c, int *width, int *height);
static void window_size_history_store(Client *c, int width, int height);

static void motionabsolute(struct wl_listener *listener, void *data);
static void motionnotify(uint32_t time, struct wlr_input_device *device, double sx,
		double sy, double sx_unaccel, double sy_unaccel);
static void motionrelative(struct wl_listener *listener, void *data);
static void moveresize(const Arg *arg);
static void opacityadjust(const Arg *arg);
static void setopacity(Client *c, float opacity);
static void applyopacity(Client *c);
static void outputmgrapply(struct wl_listener *listener, void *data);
static void outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test);
static void outputmgrtest(struct wl_listener *listener, void *data);
static void pointerfocus(Client *c, struct wlr_surface *surface,
		double sx, double sy, uint32_t time);
static void printstatus(void);
static void powermgrsetmode(struct wl_listener *listener, void *data);
static void quit(const Arg *arg);
static void viewport_pan(const Arg *arg);
static void viewport_zoom(const Arg *arg);
static void viewport_reset(const Arg *arg);
static void viewport_fit_all(const Arg *arg);
void viewport_center_on(Client *c);
static void viewport_toggle_follow(const Arg *arg);
static void viewport_toggle_follow_new(const Arg *arg);
static void viewport_follow_focus(void);
static void viewport_tick(void);
static void wallpaper_configure(int w, int h);
static void wallpaper_update(void);
static void cropbegin(const Arg *arg);
static void cropcancel(const Arg *arg);
static void cropend(const Arg *arg);
static void cropdraw(void);
static void rendermon(struct wl_listener *listener, void *data);
static void requestdecorationmode(struct wl_listener *listener, void *data);
static void requeststartdrag(struct wl_listener *listener, void *data);
static void requestmonstate(struct wl_listener *listener, void *data);
void resize(Client *c, struct wlr_box geo, int interact);
static void run(char *startup_cmd);
static void setcursor(struct wl_listener *listener, void *data);
static void setcursorshape(struct wl_listener *listener, void *data);
static void setfloating(Client *c, int floating);
void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
void resizefocused(const Arg *arg);
static void setmon(Client *c, Monitor *m);
static void setpsel(struct wl_listener *listener, void *data);
static void setsel(struct wl_listener *listener, void *data);
static void setup(void);
static void spawn(const Arg *arg);
static void startdrag(struct wl_listener *listener, void *data);
static void tagmon(const Arg *arg);
static void infinite(Monitor *m);
static void togglefloating(const Arg *arg);
static void togglefullscreen(const Arg *arg);
static void unlocksession(struct wl_listener *listener, void *data);
static void unmaplayersurfacenotify(struct wl_listener *listener, void *data);
static void unmapnotify(struct wl_listener *listener, void *data);
static void updatemons(struct wl_listener *listener, void *data);
static void updatetitle(struct wl_listener *listener, void *data);
static void urgent(struct wl_listener *listener, void *data);
static void virtualkeyboard(struct wl_listener *listener, void *data);
static void virtualpointer(struct wl_listener *listener, void *data);
static Monitor *xytomon(double x, double y);
static void xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny);
static void zoom(const Arg *arg);

#define SUPER_TAP_MAX_MS 250
static struct {
	int down;
	int consumed;
	uint32_t press_time_msec;
} super_tap;

static pid_t launcher_pid = -1;

/* forward declare PTY registration so spawn_pid can call it */
static void pty_register(pid_t pid, int master_fd, const char *cmd);

static pid_t
spawn_pid(const Arg *arg)
{
	pid_t pid = -1;
	const char *cmd;
	int master = -1, slave = -1;

	if (!arg || !arg->v)
		return -1;

	cmd = ((char **)arg->v)[0];

	if (openpty(&master, &slave, NULL, NULL, NULL) < 0) {
		wlr_log(WLR_ERROR, "openpty failed: %s", strerror(errno));
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		wlr_log(WLR_ERROR, "Failed to fork for spawn: %s", strerror(errno));
		close(master);
		close(slave);
		return -1;
	}

	if (pid == 0) {
		/* Child: make slave the controlling tty and exec */
		setsid();
		if (ioctl(slave, TIOCSCTTY, 0) < 0) {
			/* non-fatal */
		}
		dup2(slave, STDIN_FILENO);
		dup2(slave, STDOUT_FILENO);
		dup2(slave, STDERR_FILENO);
		close(master);
		close(slave);
		execvp(cmd, (char **)arg->v);
		wlr_log(WLR_ERROR, "Failed to execvp %s: %s", cmd, strerror(errno));
		_exit(1);
	}

	/* Parent: keep master for reading logs, close slave */
	close(slave);
	if (master >= 0) {
		int flags = fcntl(master, F_GETFL, 0);
		fcntl(master, F_SETFL, flags | O_NONBLOCK);
		/* register the PTY master for reading in event loop */
		/* pty_register will be defined later in this file */
		pty_register(pid, master, cmd);
	}

	return pid;
}

/* variables */
static pid_t child_pid = -1;
static int locked;
static void *exclusive_focus;
static struct wl_display *dpy;
static struct wl_event_loop *event_loop;
static struct wlr_backend *backend;
static struct wlr_scene *scene;
static struct wlr_scene_tree *layers[NUM_LAYERS];
static struct wlr_scene_tree *drag_icon;
/* Map from ZWLR_LAYER_SHELL_* constants to Lyr* enum */
static const int layermap[] = { LyrBg, LyrBottom, LyrTop, LyrOverlay };
static struct wlr_renderer *drw;
static struct wlr_allocator *alloc;
static struct wlr_compositor *compositor;
static struct wlr_session *session;

static struct wlr_xdg_shell *xdg_shell;
static struct wlr_xdg_activation_v1 *activation;
struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_mgr;
static struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
struct wl_list clients; /* tiling order */
static struct wl_list fstack;  /* focus order */
struct wl_list static_listeners;
static struct wlr_idle_notifier_v1 *idle_notifier;
static struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
static struct wlr_layer_shell_v1 *layer_shell;
static struct wlr_output_manager_v1 *output_mgr;
static struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;
static struct wlr_virtual_pointer_manager_v1 *virtual_pointer_mgr;
static struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr;
static struct wlr_output_power_manager_v1 *power_mgr;

static struct wlr_pointer_constraints_v1 *pointer_constraints;
static struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;
static struct wlr_pointer_constraint_v1 *active_constraint;

static struct wlr_cursor *cursor;
static struct wlr_xcursor_manager *cursor_mgr;

static struct wlr_scene_rect *root_bg;
static struct wlr_session_lock_manager_v1 *session_lock_mgr;
static struct wlr_scene_rect *locked_bg;
static struct wlr_session_lock_v1 *cur_lock;

static struct wlr_seat *seat;
static KeyboardGroup *kb_group;
static unsigned int cursor_mode;
static Client *grabc;
static int grabcx, grabcy; /* client-relative */

static struct wlr_output_layout *output_layout;
static struct wlr_box sgeom;
struct wl_list mons;
Monitor *selmon;

/* global event handlers */
static struct wl_listener cursor_axis = {.notify = axisnotify};
static struct wl_listener cursor_button = {.notify = buttonpress};
static struct wl_listener cursor_frame = {.notify = cursorframe};
static struct wl_listener cursor_motion = {.notify = motionrelative};
static struct wl_listener cursor_motion_absolute = {.notify = motionabsolute};
static struct wl_listener gpu_reset = {.notify = gpureset};
static struct wl_listener layout_change = {.notify = updatemons};
static struct wl_listener new_idle_inhibitor = {.notify = createidleinhibitor};
static struct wl_listener new_input_device = {.notify = inputdevice};
static struct wl_listener new_virtual_keyboard = {.notify = virtualkeyboard};
static struct wl_listener new_virtual_pointer = {.notify = virtualpointer};
static struct wl_listener new_pointer_constraint = {.notify = createpointerconstraint};
static struct wl_listener new_output = {.notify = createmon};
static struct wl_listener new_xdg_toplevel = {.notify = createnotify};
static struct wl_listener new_xdg_popup = {.notify = createpopup};
static struct wl_listener new_xdg_decoration = {.notify = createdecoration};
static struct wl_listener new_layer_surface = {.notify = createlayersurface};
static struct wl_listener output_mgr_apply = {.notify = outputmgrapply};
static struct wl_listener output_mgr_test = {.notify = outputmgrtest};
static struct wl_listener output_power_mgr_set_mode = {.notify = powermgrsetmode};
static struct wl_listener request_activate = {.notify = urgent};
static struct wl_listener request_cursor = {.notify = setcursor};
static struct wl_listener request_set_psel = {.notify = setpsel};
static struct wl_listener request_set_sel = {.notify = setsel};
static struct wl_listener request_set_cursor_shape = {.notify = setcursorshape};
static struct wl_listener request_start_drag = {.notify = requeststartdrag};
static struct wl_listener start_drag = {.notify = startdrag};
static struct wl_listener new_session_lock = {.notify = locksession};

/* Directional focus navigation - forward declaration for config.h */
static void focus_directional(const Arg *arg);

/* Column movement (Niri-style) - forward declaration for config.h */
static void move_column(const Arg *arg);

/* configuration, allows nested code to access above variables */
#include "config.h"

/* attempt to encapsulate suck into one file */
#include "client_inline.h"

/* PTY process tracking */
typedef struct PTYProc {
	pid_t pid;
	int master_fd;
	struct wl_event_source *source;
	char *cmd;
	char *logbuf;
	size_t loglen;
	struct PTYProc *next;
} PTYProc;

static PTYProc *pty_list = NULL;

static PTYProc *
pty_find(pid_t pid)
{
	PTYProc *p;

	for (p = pty_list; p; p = p->next) {
		if (p->pid == pid)
			return p;
	}

	return NULL;
}

static int
pty_append_log(PTYProc *p, const char *buf, size_t len)
{
	char *newbuf;
	size_t newlen;

	if (!p || !buf || len == 0)
		return -1;
	newlen = p->loglen + len;
	newbuf = realloc(p->logbuf, newlen + 1);
	if (!newbuf)
		return -1;
	p->logbuf = newbuf;
	memcpy(p->logbuf + p->loglen, buf, len);
	p->loglen = newlen;
	p->logbuf[p->loglen] = '\0';
	return 0;
}

int
pty_inject(pid_t pid, const char *text)
{
	PTYProc *p = pty_find(pid);
	size_t len;

	if (!p || p->master_fd < 0 || !text)
		return -1;
	len = strlen(text);
	return (int)write(p->master_fd, text, len);
}

const char *
pty_log_for(pid_t pid)
{
	PTYProc *p = pty_find(pid);

	if (!p)
		return NULL;
	return p->logbuf;
}

static int
pty_read_cb(int fd, uint32_t mask, void *data)
{
	PTYProc *p = data;
	char buf[1024];
	ssize_t n;

	(void)mask;
	if (!p) return 0;
	n = read(fd, buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = '\0';
		wlr_log(WLR_INFO, "pty[%d]: %s", p->pid, buf);
		pty_append_log(p, buf, (size_t)n);
		return 1; /* keep the source */
	}

	/* EOF or error - unregister */
	if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
		if (p->source)
			wl_event_source_remove(p->source);
		if (p->master_fd >= 0)
			close(p->master_fd);
		/* remove from list */
		PTYProc **pp = &pty_list;
		while (*pp) {
			if (*pp == p) {
				*pp = p->next;
				break;
			}
			pp = &(*pp)->next;
		}
		free(p->cmd);
		free(p->logbuf);
		free(p);
	}
	return 0;
}

static void
pty_register(pid_t pid, int master_fd, const char *cmd)
{
	PTYProc *p = calloc(1, sizeof(*p));
	if (!p) return;
	p->pid = pid;
	p->master_fd = master_fd;
	p->cmd = cmd ? strdup(cmd) : NULL;
	p->logbuf = NULL;
	p->loglen = 0;
	p->next = pty_list;
	pty_list = p;
	if (event_loop) {
		p->source = wl_event_loop_add_fd(event_loop, master_fd, WL_EVENT_READABLE, pty_read_cb, p);
	}
}

void
pty_child_reaped(pid_t pid)
{
	PTYProc *p = pty_list;
	PTYProc *prev = NULL;
	while (p) {
		if (p->pid == pid) {
			if (p->source)
				wl_event_source_remove(p->source);
			if (p->master_fd >= 0)
				close(p->master_fd);
			if (prev)
				prev->next = p->next;
			else
				pty_list = p->next;
			free(p->cmd);
			free(p->logbuf);
			free(p);
			return;
		}
		prev = p;
		p = p->next;
	}
}

/* Layout functions */
static void infinite(Monitor *m);
/* Defined in modules/layout/layout_world.c; declared here because the crop
 * module (included earlier) also uses it. */
static int same_column_x(float a, float b);

/* wlr-foreign-toplevel-management (defined in modules/foreign_toplevel.c). */
void ftl_create(Client *c);
void ftl_destroy(Client *c);
void ftl_update_title(Client *c);
void ftl_sync_state(void);

/* Shell IPC socket (defined in modules/ipc.c). */
static void ipc_init(const char *wl_display_name);
static void ipc_broadcast_state(void);
static void ipc_finish(void);

/* Hybrid window anchoring system (forward declarations before config.h) */
static void arrange_columns(Monitor *m);
static void place_window_column(Client *c, Monitor *m);

/* Buffer scaling */
static int is_integer_zoom(float zoom);
void client_set_buffer_scale(Client *c, float scale);

/* function implementations */
void
applybounds(Client *c, struct wlr_box *bbox)
{
	/* set minimum possible */
	c->geom.width = MAX(1 + 2 * (int)c->bw, c->geom.width);
	c->geom.height = MAX(1 + 2 * (int)c->bw, c->geom.height);

	if (c->geom.x >= bbox->x + bbox->width)
		c->geom.x = bbox->x + bbox->width - c->geom.width;
	if (c->geom.y >= bbox->y + bbox->height)
		c->geom.y = bbox->y + bbox->height - c->geom.height;
	if (c->geom.x + c->geom.width <= bbox->x)
		c->geom.x = bbox->x;
	if (c->geom.y + c->geom.height <= bbox->y)
		c->geom.y = bbox->y;
}

void
applyrules(Client *c)
{
	/* rule matching */
	const char *appid, *title;
	int i;
	const Rule *r;
	Monitor *mon = selmon, *m;

	appid = client_get_appid(c);
	if (!appid)
		appid = "";
	title = client_get_title(c);
	if (!title)
		title = "";

	for (r = rules; r < END(rules); r++) {
		if ((!r->title || strstr(title, r->title))
				&& (!r->id || strstr(appid, r->id))) {
			c->isfloating = r->isfloating;
			i = 0;
			wl_list_for_each(m, &mons, link) {
				if (r->monitor == i++)
					mon = m;
			}
		}
	}

	c->isfloating |= client_is_float_type(c);
	setmon(c, mon);
}

int
window_size_history_make_key(Client *c, char *buf, size_t buflen)
{
	const char *appid;
	const char *title;

	if (!c || !buf || buflen == 0)
		return 0;

	appid = client_get_appid(c);
	if (appid && appid[0] != '\0' && strcmp(appid, "broken") != 0) {
		snprintf(buf, buflen, "app:%s", appid);
		return 1;
	}

	title = client_get_title(c);
	if (title && title[0] != '\0' && strcmp(title, "broken") != 0) {
		snprintf(buf, buflen, "title:%s", title);
		return 1;
	}

	return 0;
}

int
window_size_history_load(Client *c, int *width, int *height)
{
	char key[WINDOW_SIZE_KEY_MAX];
	int i;

	if (!width || !height)
		return 0;
	if (!window_size_history_make_key(c, key, sizeof(key)))
		return 0;

	for (i = 0; i < WINDOW_SIZE_HISTORY_MAX; i++) {
		if (window_size_history.entries[i].key[0] == '\0')
			continue;
		if (strcmp(window_size_history.entries[i].key, key) != 0)
			continue;
		if (window_size_history.entries[i].width <= 0 || window_size_history.entries[i].height <= 0)
			return 0;

		*width = window_size_history.entries[i].width;
		*height = window_size_history.entries[i].height;
		window_size_history.entries[i].stamp = ++window_size_history.stamp;
		return 1;
	}

	return 0;
}

void
window_size_history_store(Client *c, int width, int height)
{
	char key[WINDOW_SIZE_KEY_MAX];
	int i;
	int target;
	int first_empty;

	if (width <= 0 || height <= 0)
		return;
	if (!window_size_history_make_key(c, key, sizeof(key)))
		return;

	first_empty = -1;
	target = -1;

	for (i = 0; i < WINDOW_SIZE_HISTORY_MAX; i++) {
		if (window_size_history.entries[i].key[0] == '\0') {
			if (first_empty < 0)
				first_empty = i;
			continue;
		}
		if (strcmp(window_size_history.entries[i].key, key) == 0) {
			target = i;
			break;
		}
	}

	if (target < 0)
		target = first_empty;
	if (target < 0) {
		target = 0;
		for (i = 1; i < WINDOW_SIZE_HISTORY_MAX; i++) {
			if (window_size_history.entries[i].stamp < window_size_history.entries[target].stamp)
				target = i;
		}
	}

	snprintf(window_size_history.entries[target].key,
		sizeof(window_size_history.entries[target].key), "%s", key);
	window_size_history.entries[target].width = width;
	window_size_history.entries[target].height = height;
	window_size_history.entries[target].stamp = ++window_size_history.stamp;
}

void
arrange(Monitor *m)
{
	Client *c;

	if (!m->wlr_output->enabled)
		return;

	wallpaper_update();

	wl_list_for_each(c, &clients, link) {
		if (c->mon == m) {
			wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m));
			client_set_suspended(c, !VISIBLEON(c, m));
		}
	}

	wlr_scene_node_set_enabled(&m->fullscreen_bg->node,
			(c = focustop(m)) && c->isfullscreen);

	snprintf(m->ltsymbol, sizeof(m->ltsymbol), "%s", m->lt[m->sellt]->symbol);

	/* We move all clients (except fullscreen and unmanaged) to LyrTile while
	 * in floating layout to avoid "real" floating clients be always on top */
	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || c->scene->node.parent == layers[LyrFS])
			continue;

		wlr_scene_node_reparent(&c->scene->node,
				(!m->lt[m->sellt]->arrange && c->isfloating)
						? layers[LyrTile]
						: (m->lt[m->sellt]->arrange && c->isfloating)
								? layers[LyrFloat]
								: c->scene->node.parent);
	}

	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
	motionnotify(0, NULL, 0, 0, 0, 0);
	checkidleinhibitor(NULL);
}

void
arrangelayer(Monitor *m, struct wl_list *list, struct wlr_box *usable_area, int exclusive)
{
	LayerSurface *l;
	struct wlr_box full_area = m->m;

	wl_list_for_each(l, list, link) {
		struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;

		if (!layer_surface->initialized)
			continue;

		if (exclusive != (layer_surface->current.exclusive_zone > 0))
			continue;

		wlr_scene_layer_surface_v1_configure(l->scene_layer, &full_area, usable_area);
		wlr_scene_node_set_position(&l->popups->node, l->scene->node.x, l->scene->node.y);
	}
}

void
arrangelayers(Monitor *m)
{
	int i;
	struct wlr_box usable_area = m->m;
	LayerSurface *l;
	uint32_t layers_above_shell[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
	};
	if (!m->wlr_output->enabled)
		return;

	/* Arrange exclusive surfaces from top->bottom */
	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 1);

	if (!wlr_box_equal(&usable_area, &m->w)) {
		m->w = usable_area;
		arrange(m);
	}

	/* Arrange non-exclusive surfaces from top->bottom */
	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 0);

	/* Find topmost layer that demands EXCLUSIVE keyboard focus, if any.
	 * Only EXCLUSIVE grabs the keyboard here; ON_DEMAND surfaces (e.g. a bar
	 * that wants click-to-type) must NOT starve toplevels of keyboard input —
	 * they receive focus via pointer interaction instead. Treating the enum as
	 * a boolean (the dwl original) wrongly grabbed for ON_DEMAND too. */
	for (i = 0; i < (int)LENGTH(layers_above_shell); i++) {
		wl_list_for_each_reverse(l, &m->layers[layers_above_shell[i]], link) {
			if (locked || l->layer_surface->current.keyboard_interactive
					!= ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
					|| !l->mapped)
				continue;
			/* Deactivate the focused client. */
			focusclient(NULL, 0);
			exclusive_focus = l;
			client_notify_enter(l->layer_surface->surface, wlr_seat_get_keyboard(seat));
			return;
		}
	}

	/* No layer surface demands exclusive keyboard focus. If one previously held
	 * it, release and hand keyboard focus back to the top client — otherwise the
	 * keyboard stays stranded on the (now non-exclusive) layer surface and no
	 * window can be typed into. */
	if (exclusive_focus) {
		exclusive_focus = NULL;
		focus_top(selmon, 0);
	}
}

void
axisnotify(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	struct wlr_pointer_axis_event *event = data;
	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
	/* TODO: allow usage of scroll wheel for mousebindings, it can be implemented
	 * by checking the event's orientation and the delta of the event */
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source, event->relative_direction);
}

void
buttonpress(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_button_event *event = data;
	struct wlr_keyboard *keyboard;
	uint32_t mods;
	Client *c;
	const Button *b;

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	switch (event->state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		selmon = xytomon(cursor->x, cursor->y);
		if (locked)
			break;
		cursor_mode = CurPressed;
		
		/* Crop mode: start selection on left click */
		if (crop_editor.active && event->button == BTN_LEFT) {
			crop_editor.dragging = true;
			crop_editor.start_x = cursor->x;
			crop_editor.start_y = cursor->y;
			crop_editor.end_x = cursor->x;
			crop_editor.end_y = cursor->y;
			return;
		}

		/* Change focus if the button was _pressed_ over a client */
		xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
		if (c && (!client_is_unmanaged(c) || client_wants_focus(c)))
			focusclient(c, 1);

		keyboard = wlr_seat_get_keyboard(seat);
		mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
		for (b = buttons; b < END(buttons); b++) {
			if (CLEANMASK(mods) == CLEANMASK(b->mod) &&
					event->button == b->button && b->func) {
				b->func(&b->arg);
				return;
			}
		}
		break;
	case WL_POINTER_BUTTON_STATE_RELEASED:
		/* Crop mode: end selection on left click release */
		if (crop_editor.active && event->button == BTN_LEFT && crop_editor.dragging) {
			crop_editor.end_x = cursor->x;
			crop_editor.end_y = cursor->y;
			cropend(NULL);
			return;
		}
		
		/* If you released any buttons, we exit interactive move/resize mode. */
		/* TODO: should reset to the pointer focus's current setcursor */
		if (!locked && cursor_mode != CurNormal && cursor_mode != CurPressed) {
			wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
			cursor_mode = CurNormal;
			/* Drop the window off on its new monitor */
			selmon = xytomon(cursor->x, cursor->y);
			setmon(grabc, selmon);
			grabc = NULL;
			return;
		}
		cursor_mode = CurNormal;
		break;
	}
	/* If the event wasn't handled by the compositor, notify the client with
	 * pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(seat,
			event->time_msec, event->button, event->state);
}

void
chvt(const Arg *arg)
{
	wlr_session_change_vt(session, arg->ui);
}

void
checkidleinhibitor(struct wlr_surface *exclude)
{
	int inhibited = 0, unused_lx, unused_ly;
	struct wlr_idle_inhibitor_v1 *inhibitor;
	wl_list_for_each(inhibitor, &idle_inhibit_mgr->inhibitors, link) {
		struct wlr_surface *surface = wlr_surface_get_root_surface(inhibitor->surface);
		struct wlr_scene_tree *tree = surface->data;
		if (exclude != surface && (bypass_surface_visibility || (!tree
				|| wlr_scene_node_coords(&tree->node, &unused_lx, &unused_ly)))) {
			inhibited = 1;
			break;
		}
	}

	wlr_idle_notifier_v1_set_inhibited(idle_notifier, inhibited);
}

void
cleanup(void)
{
	ipc_finish();
	cleanuplisteners();
	wl_display_destroy_clients(dpy);
	if (child_pid > 0) {
		kill(-child_pid, SIGTERM);
		waitpid(child_pid, NULL, 0);
	}
	wlr_xcursor_manager_destroy(cursor_mgr);

	destroykeyboardgroup(&kb_group->destroy, NULL);

	/* If it's not destroyed manually, it will cause a use-after-free of wlr_seat.
	 * Destroy it until it's fixed on the wlroots side */
	wlr_backend_destroy(backend);

	wl_display_destroy(dpy);
	/* Destroy after the wayland display (when the monitors are already destroyed)
	   to avoid destroying them with an invalid scene output. */
	wlr_scene_node_destroy(&scene->tree.node);
}

void
cleanupmon(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, destroy);
	LayerSurface *l, *tmp;
	size_t i;

	/* m->layers[i] are intentionally not unlinked */
	for (i = 0; i < LENGTH(m->layers); i++) {
		wl_list_for_each_safe(l, tmp, &m->layers[i], link)
			wlr_layer_surface_v1_destroy(l->layer_surface);
	}

	wl_list_remove(&m->destroy.link);
	wl_list_remove(&m->frame.link);
	wl_list_remove(&m->link);
	wl_list_remove(&m->request_state.link);
	if (m->lock_surface)
		destroylocksurface(&m->destroy_lock_surface, NULL);
	m->wlr_output->data = NULL;
	wlr_output_layout_remove(output_layout, m->wlr_output);
	wlr_scene_output_destroy(m->scene_output);

	closemon(m);
	wlr_scene_node_destroy(&m->fullscreen_bg->node);
	free(m);
}

void
cleanuplisteners(void)
{
	StaticListener *sl, *tmp;

	wl_list_remove(&cursor_axis.link);
	wl_list_remove(&cursor_button.link);
	wl_list_remove(&cursor_frame.link);
	wl_list_remove(&cursor_motion.link);
	wl_list_remove(&cursor_motion_absolute.link);
	wl_list_remove(&gpu_reset.link);
	wl_list_remove(&new_idle_inhibitor.link);
	wl_list_remove(&layout_change.link);
	wl_list_remove(&new_input_device.link);
	wl_list_remove(&new_virtual_keyboard.link);
	wl_list_remove(&new_virtual_pointer.link);
	wl_list_remove(&new_pointer_constraint.link);
	wl_list_remove(&new_output.link);
	wl_list_remove(&new_xdg_toplevel.link);
	wl_list_remove(&new_xdg_decoration.link);
	wl_list_remove(&new_xdg_popup.link);
	wl_list_remove(&new_layer_surface.link);
	wl_list_remove(&output_mgr_apply.link);
	wl_list_remove(&output_mgr_test.link);
	wl_list_remove(&output_power_mgr_set_mode.link);
	wl_list_remove(&request_activate.link);
	wl_list_remove(&request_cursor.link);
	wl_list_remove(&request_set_psel.link);
	wl_list_remove(&request_set_sel.link);
	wl_list_remove(&request_set_cursor_shape.link);
	wl_list_remove(&request_start_drag.link);
	wl_list_remove(&start_drag.link);
	wl_list_remove(&new_session_lock.link);

	wl_list_for_each_safe(sl, tmp, &static_listeners, link) {
		wl_list_remove(&sl->listener.link);
		wl_list_remove(&sl->link);
		free(sl);
	}
}

void
closemon(Monitor *m)
{
	/* update selmon if needed and
	 * move closed monitor's clients to the focused one */
	Client *c;
	int i = 0, nmons = wl_list_length(&mons);
	if (!nmons) {
		selmon = NULL;
	} else if (m == selmon) {
		do /* don't switch to disabled mons */
			selmon = wl_container_of(mons.next, selmon, link);
		while (!selmon->wlr_output->enabled && i++ < nmons);

		if (!selmon->wlr_output->enabled)
			selmon = NULL;
	}

	wl_list_for_each(c, &clients, link) {
		if (c->isfloating && c->geom.x > m->m.width)
			resize(c, (struct wlr_box){.x = c->geom.x - m->w.width, .y = c->geom.y,
					.width = c->geom.width, .height = c->geom.height}, 0);
		if (c->mon == m)
			setmon(c, selmon);
	}
	focus_top(selmon, 1);
	printstatus();
}

void
commitlayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, surface_commit);
	struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;
	struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->current.layer]];
	struct wlr_layer_surface_v1_state old_state;

	if (l->layer_surface->initial_commit) {
		client_set_scale(layer_surface->surface, l->mon->wlr_output->scale);

		/* Temporarily set the layer's current state to pending
		 * so that we can easily arrange it */
		old_state = l->layer_surface->current;
		l->layer_surface->current = l->layer_surface->pending;
		arrangelayers(l->mon);
		l->layer_surface->current = old_state;
		return;
	}

	if (layer_surface->current.committed == 0 && l->mapped == layer_surface->surface->mapped)
		return;
	l->mapped = layer_surface->surface->mapped;

	if (scene_layer != l->scene->node.parent) {
		wlr_scene_node_reparent(&l->scene->node, scene_layer);
		wl_list_remove(&l->link);
		wl_list_insert(&l->mon->layers[layer_surface->current.layer], &l->link);
		wlr_scene_node_reparent(&l->popups->node, (layer_surface->current.layer
				< ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer));
	}

	arrangelayers(l->mon);
}

void
commitnotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, commit);

	if (c->surface.xdg->initial_commit) {
		/*
		 * Get the monitor this client will be rendered on
		 * Note that if the user set a rule in which the client is placed on
		 * a different monitor based on its title, this will likely select
		 * a wrong monitor.
		 */
		applyrules(c);
		if (c->mon) {
			client_set_scale(client_surface(c), c->mon->wlr_output->scale);
		}
		setmon(c, NULL); /* Make sure to reapply rules in mapnotify() */

		wlr_xdg_toplevel_set_wm_capabilities(c->surface.xdg->toplevel,
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
		if (c->decoration)
			requestdecorationmode(&c->set_decoration_mode, c->decoration);
		wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, 0, 0);
		return;
	}

	/* Allow floating clients to apply their own requested content size
	 * (e.g. apps restoring last window size) while compositor still controls
	 * placement, bounds, fullscreen, and tiled layouts. */
	client_accept_requested_size(c);

	resize(c, c->geom, (c->isfloating && !c->isfullscreen));

	/* keep a non-opaque window's opacity applied to freshly committed buffers */
	if (c->opacity < 1.0f)
		applyopacity(c);

	/* mark a pending resize as completed */
	if (c->resize && c->resize <= c->surface.xdg->current.configure_serial)
		c->resize = 0;
}

void
commitpopup(struct wl_listener *listener, void *data)
{
	struct wlr_surface *surface = data;
	struct wlr_xdg_popup *popup = wlr_xdg_popup_try_from_wlr_surface(surface);
	LayerSurface *l = NULL;
	Client *c = NULL;
	struct wlr_box box;
	int type = -1;

	if (!popup)
		return;

	if (!popup->base->initial_commit)
		return;

	type = toplevel_from_wlr_surface(popup->base->surface, &c, &l);
	if (!popup->parent || type < 0)
		return;
	popup->base->surface->data = wlr_scene_xdg_surface_create(
			popup->parent->data, popup->base);
	if ((l && !l->mon) || (c && !c->mon)) {
		wlr_xdg_popup_destroy(popup);
		return;
	}
	box = type == LayerShell ? l->mon->m : c->mon->w;
	box.x -= (type == LayerShell ? l->scene->node.x : c->geom.x);
	box.y -= (type == LayerShell ? l->scene->node.y : c->geom.y);
	wlr_xdg_popup_unconstrain_from_box(popup, &box);
	static_listener_free(listener);
}

void
createdecoration(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_decoration_v1 *deco = data;
	Client *c = deco->toplevel->base->data;
	c->decoration = deco;

	LISTEN(&deco->events.request_mode, &c->set_decoration_mode, requestdecorationmode);
	LISTEN(&deco->events.destroy, &c->destroy_decoration, destroydecoration);

	requestdecorationmode(&c->set_decoration_mode, deco);
}

void
createidleinhibitor(struct wl_listener *listener, void *data)
{
	struct wlr_idle_inhibitor_v1 *idle_inhibitor = data;
	LISTEN_STATIC(&idle_inhibitor->events.destroy, destroyidleinhibitor);

	checkidleinhibitor(NULL);
}

void
createkeyboard(struct wlr_keyboard *keyboard)
{
	/* Set the keymap to match the group keymap */
	wlr_keyboard_set_keymap(keyboard, kb_group->wlr_group->keyboard.keymap);

	/* Add the new keyboard to the group */
	wlr_keyboard_group_add_keyboard(kb_group->wlr_group, keyboard);
}

KeyboardGroup *
createkeyboardgroup(void)
{
	KeyboardGroup *group = ecalloc(1, sizeof(*group));
	struct xkb_context *context;
	struct xkb_keymap *keymap;

	group->wlr_group = wlr_keyboard_group_create();
	group->wlr_group->data = group;

	/* Prepare an XKB keymap and assign it to the keyboard group. */
	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!(keymap = xkb_keymap_new_from_names(context, &xkb_rules,
				XKB_KEYMAP_COMPILE_NO_FLAGS)))
		die("failed to compile keymap");

	wlr_keyboard_set_keymap(&group->wlr_group->keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);

	wlr_keyboard_set_repeat_info(&group->wlr_group->keyboard, repeat_rate, repeat_delay);

	/* Set up listeners for keyboard events */
	LISTEN(&group->wlr_group->keyboard.events.key, &group->key, keypress);
	LISTEN(&group->wlr_group->keyboard.events.modifiers, &group->modifiers, keypressmod);

	group->key_repeat_source = wl_event_loop_add_timer(event_loop, keyrepeat, group);

	/* A seat can only have one keyboard, but this is a limitation of the
	 * Wayland protocol - not wlroots. We assign all connected keyboards to the
	 * same wlr_keyboard_group, which provides a single wlr_keyboard interface for
	 * all of them. Set this combined wlr_keyboard as the seat keyboard.
	 */
	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	return group;
}

void
createlayersurface(struct wl_listener *listener, void *data)
{
	struct wlr_layer_surface_v1 *layer_surface = data;
	LayerSurface *l;
	Monitor *m;
	struct wlr_surface *surface = layer_surface->surface;
	struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->pending.layer]];

	if (!layer_surface->output
			&& !(layer_surface->output = selmon ? selmon->wlr_output : NULL)) {
		wlr_layer_surface_v1_destroy(layer_surface);
		return;
	}

	m = layer_surface->output ? layer_surface->output->data : NULL;
	if (!m) {
		wlr_layer_surface_v1_destroy(layer_surface);
		return;
	}

	l = ecalloc(1, sizeof(*l));
	layer_surface->data = l;
	l->type = LayerShell;
	l->layer_surface = layer_surface;
	l->mon = m;
	l->scene_layer = wlr_scene_layer_surface_v1_create(scene_layer, layer_surface);
	if (!l->scene_layer) {
		layer_surface->data = NULL;
		free(l);
		wlr_layer_surface_v1_destroy(layer_surface);
		return;
	}
	l->scene = l->scene_layer->tree;
	l->popups = surface->data = wlr_scene_tree_create(layer_surface->current.layer
			< ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer);
	if (!l->popups) {
		wlr_scene_node_destroy(&l->scene->node);
		layer_surface->data = NULL;
		free(l);
		wlr_layer_surface_v1_destroy(layer_surface);
		return;
	}
	l->scene->node.data = l->popups->node.data = l;

	LISTEN(&surface->events.commit, &l->surface_commit, commitlayersurfacenotify);
	LISTEN(&surface->events.unmap, &l->unmap, unmaplayersurfacenotify);
	LISTEN(&layer_surface->events.destroy, &l->destroy, destroylayersurfacenotify);

	wl_list_insert(&l->mon->layers[layer_surface->pending.layer],&l->link);
	wlr_surface_send_enter(surface, layer_surface->output);
}

void
createlocksurface(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, new_surface);
	struct wlr_session_lock_surface_v1 *lock_surface = data;
	Monitor *m = lock_surface->output->data;
	struct wlr_scene_tree *scene_tree = lock_surface->surface->data
			= wlr_scene_subsurface_tree_create(lock->scene, lock_surface->surface);
	m->lock_surface = lock_surface;

	wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
	wlr_session_lock_surface_v1_configure(lock_surface, m->m.width, m->m.height);

	LISTEN(&lock_surface->events.destroy, &m->destroy_lock_surface, destroylocksurface);

	if (m == selmon)
		client_notify_enter(lock_surface->surface, wlr_seat_get_keyboard(seat));
}

void
createmon(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new output (aka a display or
	 * monitor) becomes available. */
	struct wlr_output *wlr_output = data;
	const MonitorRule *r;
	size_t i;
	struct wlr_output_state state;
	Monitor *m;

	if (!wlr_output_init_render(wlr_output, alloc, drw))
		return;

	m = wlr_output->data = ecalloc(1, sizeof(*m));
	m->wlr_output = wlr_output;

	for (i = 0; i < LENGTH(m->layers); i++)
		wl_list_init(&m->layers[i]);

	wlr_output_state_init(&state);
	/* Initialize monitor state using configured rules */
	for (r = monrules; r < END(monrules); r++) {
		if (!r->name || strstr(wlr_output->name, r->name)) {
			m->m.x = r->x;
			m->m.y = r->y;
			m->lt[0] = r->lt;
			m->lt[1] = &layouts[LENGTH(layouts) > 1 && r->lt != &layouts[1]];
			snprintf(m->ltsymbol, sizeof(m->ltsymbol), "%s", m->lt[m->sellt]->symbol);
			wlr_output_state_set_scale(&state, r->scale);
			wlr_output_state_set_transform(&state, r->rr);
			break;
		}
	}

	/* The mode is a tuple of (width, height, refresh rate), and each
	 * monitor supports only a specific set of modes. We just pick the
	 * monitor's preferred mode; a more sophisticated compositor would let
	 * the user configure it. */
	wlr_output_state_set_mode(&state, wlr_output_preferred_mode(wlr_output));

	/* Set up event listeners */
	LISTEN(&wlr_output->events.frame, &m->frame, rendermon);
	LISTEN(&wlr_output->events.destroy, &m->destroy, cleanupmon);
	LISTEN(&wlr_output->events.request_state, &m->request_state, requestmonstate);

	wlr_output_state_set_enabled(&state, 1);
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	wl_list_insert(&mons, &m->link);
	printstatus();

	/* The xdg-protocol specifies:
	 *
	 * If the fullscreened surface is not opaque, the compositor must make
	 * sure that other screen content not part of the same surface tree (made
	 * up of subsurfaces, popups or similarly coupled surfaces) are not
	 * visible below the fullscreened surface.
	 *
	 */
	/* updatemons() will resize and set correct position */
	m->fullscreen_bg = wlr_scene_rect_create(layers[LyrFS], 0, 0, fullscreen_bg);
	wlr_scene_node_set_enabled(&m->fullscreen_bg->node, 0);

	/* Adds this to the output layout in the order it was configured.
	 *
	 * The output layout utility automatically adds a wl_output global to the
	 * display, which Wayland clients can see to find out information about the
	 * output (such as DPI, scale factor, manufacturer, etc).
	 */
	m->scene_output = wlr_scene_output_create(scene, wlr_output);
	if (m->m.x == -1 && m->m.y == -1)
		wlr_output_layout_add_auto(output_layout, wlr_output);
	else
		wlr_output_layout_add(output_layout, wlr_output, m->m.x, m->m.y);
}

void
createnotify(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client creates a new toplevel (application window). */
	struct wlr_xdg_toplevel *toplevel = data;
	Client *c = NULL;
	
	if (toplevel->base->data) {
		wlr_log(WLR_ERROR, "Duplicate createnotify for xdg_toplevel; keeping existing client");
		return;
	}

	/* Allocate a Client for this surface */
	c = toplevel->base->data = ecalloc(1, sizeof(*c));

	c->type = XDGShell;
	c->surface.xdg = toplevel->base;
	c->bw = borderpx;
	c->opacity = 1.0f;
	wl_list_init(&c->link);
	wl_list_init(&c->flink);

	LISTEN(&toplevel->base->surface->events.commit, &c->commit, commitnotify);
	LISTEN(&toplevel->base->surface->events.map, &c->map, mapnotify);
	LISTEN(&toplevel->base->surface->events.unmap, &c->unmap, unmapnotify);
	LISTEN(&toplevel->events.destroy, &c->destroy, destroynotify);
	LISTEN(&toplevel->events.request_fullscreen, &c->fullscreen, fullscreennotify);
	LISTEN(&toplevel->events.request_maximize, &c->maximize, maximizenotify);
	LISTEN(&toplevel->events.set_title, &c->set_title, updatetitle);
	
	wlr_log(WLR_DEBUG, "Created new client for XDG toplevel");
}

void
createpointer(struct wlr_pointer *pointer)
{
	struct libinput_device *device;
	if (wlr_input_device_is_libinput(&pointer->base)
			&& (device = wlr_libinput_get_device_handle(&pointer->base))) {

		if (libinput_device_config_tap_get_finger_count(device)) {
			libinput_device_config_tap_set_enabled(device, tap_to_click);
			libinput_device_config_tap_set_drag_enabled(device, tap_and_drag);
			libinput_device_config_tap_set_drag_lock_enabled(device, drag_lock);
			libinput_device_config_tap_set_button_map(device, button_map);
		}

		if (libinput_device_config_scroll_has_natural_scroll(device))
			libinput_device_config_scroll_set_natural_scroll_enabled(device, natural_scrolling);

		if (libinput_device_config_dwt_is_available(device))
			libinput_device_config_dwt_set_enabled(device, disable_while_typing);

		if (libinput_device_config_left_handed_is_available(device))
			libinput_device_config_left_handed_set(device, left_handed);

		if (libinput_device_config_middle_emulation_is_available(device))
			libinput_device_config_middle_emulation_set_enabled(device, middle_button_emulation);

		if (libinput_device_config_scroll_get_methods(device) != LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
			libinput_device_config_scroll_set_method(device, scroll_method);

		if (libinput_device_config_click_get_methods(device) != LIBINPUT_CONFIG_CLICK_METHOD_NONE)
			libinput_device_config_click_set_method(device, click_method);

		if (libinput_device_config_send_events_get_modes(device))
			libinput_device_config_send_events_set_mode(device, send_events_mode);

		if (libinput_device_config_accel_is_available(device)) {
			libinput_device_config_accel_set_profile(device, accel_profile);
			libinput_device_config_accel_set_speed(device, accel_speed);
		}
	}

	wlr_cursor_attach_input_device(cursor, &pointer->base);
}

void
createpointerconstraint(struct wl_listener *listener, void *data)
{
	PointerConstraint *pointer_constraint = ecalloc(1, sizeof(*pointer_constraint));
	pointer_constraint->constraint = data;
	LISTEN(&pointer_constraint->constraint->events.destroy,
			&pointer_constraint->destroy, destroypointerconstraint);
}

void
createpopup(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client (either xdg-shell or layer-shell)
	 * creates a new popup. */
	struct wlr_xdg_popup *popup = data;
	LISTEN_STATIC(&popup->base->surface->events.commit, commitpopup);
}

void
cursorconstrain(struct wlr_pointer_constraint_v1 *constraint)
{
	if (active_constraint == constraint)
		return;

	if (active_constraint)
		wlr_pointer_constraint_v1_send_deactivated(active_constraint);

	active_constraint = constraint;
	wlr_pointer_constraint_v1_send_activated(constraint);
}

void
cursorframe(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits a frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen at the
	 * same time, in which case a frame event won't be sent in between. */
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(seat);
}

void
cursorwarptohint(void)
{
	Client *c = NULL;
	double sx;
	double sy;

	if (!active_constraint)
		return;
	sx = active_constraint->current.cursor_hint.x;
	sy = active_constraint->current.cursor_hint.y;

	toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
	if (c && active_constraint->current.cursor_hint.enabled) {
		wlr_cursor_warp(cursor, NULL, sx + c->geom.x + c->bw, sy + c->geom.y + c->bw);
		wlr_seat_pointer_warp(active_constraint->seat, sx, sy);
	}
}

void
destroydecoration(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, destroy_decoration);

	wl_list_remove(&c->destroy_decoration.link);
	wl_list_remove(&c->set_decoration_mode.link);
}

void
destroydragicon(struct wl_listener *listener, void *data)
{
	/* Focus enter isn't sent during drag, so refocus the focused node. */
	focus_top(selmon, 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
	static_listener_free(listener);
}

void
destroyidleinhibitor(struct wl_listener *listener, void *data)
{
	/* `data` is the wlr_surface of the idle inhibitor being destroyed,
	 * at this point the idle inhibitor is still in the list of the manager */
	checkidleinhibitor(wlr_surface_get_root_surface(data));
	static_listener_free(listener);
}

void
destroylayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, destroy);

	wl_list_remove(&l->link);
	wl_list_remove(&l->destroy.link);
	wl_list_remove(&l->unmap.link);
	wl_list_remove(&l->surface_commit.link);
	/* The scene tree owned by wlr_scene_layer_surface_v1_create() is destroyed
	 * automatically by wlroots when the layer surface is destroyed. */
	wlr_scene_node_destroy(&l->popups->node);
	free(l);
}

void
destroylock(SessionLock *lock, int unlock)
{
	wlr_seat_keyboard_notify_clear_focus(seat);
	if ((locked = !unlock))
		goto destroy;

	wlr_scene_node_set_enabled(&locked_bg->node, 0);

	focus_top(selmon, 0);
	motionnotify(0, NULL, 0, 0, 0, 0);

destroy:
	wl_list_remove(&lock->new_surface.link);
	wl_list_remove(&lock->unlock.link);
	wl_list_remove(&lock->destroy.link);

	wlr_scene_node_destroy(&lock->scene->node);
	cur_lock = NULL;
	free(lock);
}

void
destroylocksurface(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, destroy_lock_surface);
	struct wlr_session_lock_surface_v1 *surface, *lock_surface = m->lock_surface;

	m->lock_surface = NULL;
	wl_list_remove(&m->destroy_lock_surface.link);

	if (lock_surface->surface != seat->keyboard_state.focused_surface)
		return;

	if (locked && cur_lock && !wl_list_empty(&cur_lock->surfaces)) {
		surface = wl_container_of(cur_lock->surfaces.next, surface, link);
		client_notify_enter(surface->surface, wlr_seat_get_keyboard(seat));
	} else if (!locked) {
		focus_top(selmon, 1);
	} else {
		wlr_seat_keyboard_clear_focus(seat);
	}
}

void
destroynotify(struct wl_listener *listener, void *data)
{
	/* Called when the xdg_toplevel is destroyed. */
	Client *c = wl_container_of(listener, c, destroy);
	wl_list_remove(&c->destroy.link);
	wl_list_remove(&c->set_title.link);
	wl_list_remove(&c->fullscreen.link);
	wl_list_remove(&c->commit.link);
	wl_list_remove(&c->map.link);
	wl_list_remove(&c->unmap.link);
	wl_list_remove(&c->maximize.link);
	free(c);
}

void
destroypointerconstraint(struct wl_listener *listener, void *data)
{
	PointerConstraint *pointer_constraint = wl_container_of(listener, pointer_constraint, destroy);

	if (active_constraint == pointer_constraint->constraint) {
		cursorwarptohint();
		active_constraint = NULL;
	}

	wl_list_remove(&pointer_constraint->destroy.link);
	free(pointer_constraint);
}

void
destroysessionlock(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, destroy);
	destroylock(lock, 0);
}

void
destroykeyboardgroup(struct wl_listener *listener, void *data)
{
	KeyboardGroup *group = wl_container_of(listener, group, destroy);
	free(group->keysyms);
	group->keysyms = NULL;
	group->nsyms = 0;
	wl_event_source_remove(group->key_repeat_source);
	wl_list_remove(&group->key.link);
	wl_list_remove(&group->modifiers.link);
	wl_list_remove(&group->destroy.link);
	wlr_keyboard_group_destroy(group->wlr_group);
	free(group);
}

Monitor *
dirtomon(enum wlr_direction dir)
{
	struct wlr_output *next;
	if (!wlr_output_layout_get(output_layout, selmon->wlr_output))
		return selmon;
	if ((next = wlr_output_layout_adjacent_output(output_layout,
			dir, selmon->wlr_output, selmon->m.x, selmon->m.y)))
		return next->data;
	if ((next = wlr_output_layout_farthest_output(output_layout,
			dir ^ (WLR_DIRECTION_LEFT|WLR_DIRECTION_RIGHT),
			selmon->wlr_output, selmon->m.x, selmon->m.y)))
		return next->data;
	return selmon;
}

void
focusclient(Client *c, int lift)
{
	struct wlr_surface *old = seat->keyboard_state.focused_surface;
	int unused_lx, unused_ly, old_client_type;
	Client *old_c = NULL;
	LayerSurface *old_l = NULL;

	if (locked)
		return;

	/* Raise client in stacking order if requested */
	if (c && lift)
		wlr_scene_node_raise_to_top(&c->scene->node);

	if (c && client_surface(c) == old)
		return;

	if ((old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l)) == XDGShell) {
		struct wlr_xdg_popup *popup, *tmp;
		wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
			wlr_xdg_popup_destroy(popup);
	}

	/* Put the new client atop the focus stack and select its monitor */
	if (c && !client_is_unmanaged(c)) {
		wl_list_remove(&c->flink);
		wl_list_insert(&fstack, &c->flink);
		selmon = c->mon;
		c->isurgent = 0;

		/* Don't change border color if there is an exclusive focus or we are
		 * handling a drag operation */
		if (!exclusive_focus && !seat->drag) {
			client_set_border_color(c, focuscolor);
			client_set_focus_ring(c, 1);
		}
	}

	/* Deactivate old client if focus is changing */
	if (old && (!c || client_surface(c) != old)) {
		/* If an overlay is focused, don't focus or activate the client,
		 * but only update its position in fstack to render its border with focuscolor
		 * and focus it after the overlay is closed. */
		if (old_client_type == LayerShell && wlr_scene_node_coords(
					&old_l->scene->node, &unused_lx, &unused_ly)
				&& old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
			return;
		} else if (old_c && old_c == exclusive_focus && client_wants_focus(old_c)) {
			return;
		/* Don't deactivate old client if the new one wants focus, as this causes issues with winecfg
		 * and probably other clients */
		} else if (old_c && !client_is_unmanaged(old_c) && (!c || !client_wants_focus(c))) {
			client_set_border_color(old_c, bordercolor);
			client_set_focus_ring(old_c, 0);

			client_activate_surface(old, 0);
		}
	}
	printstatus();

	if (!c) {
		/* With no client, all we have left is to clear focus */
		wlr_seat_keyboard_notify_clear_focus(seat);
		return;
	}

	/* Change cursor surface */
	motionnotify(0, NULL, 0, 0, 0, 0);

	/* Have a client, so focus its top-level wlr_surface */
	client_notify_enter(client_surface(c), wlr_seat_get_keyboard(seat));

	/* Activate the new client */
	client_activate_surface(client_surface(c), 1);
	
	/* Camera follow mode - center on focused window */
	viewport_follow_focus();
}

void
focusmon(const Arg *arg)
{
	int i = 0, nmons = wl_list_length(&mons);
	if (nmons) {
		do /* don't switch to disabled mons */
			selmon = dirtomon(arg->i);
		while (!selmon->wlr_output->enabled && i++ < nmons);
	}
	focus_top(selmon, 1);
}

void
focusstack(const Arg *arg)
{
	/* Focus the next or previous client (in tiling order) on selmon */
	Client *c, *sel = focustop(selmon);
	if (!sel || (sel->isfullscreen && !client_has_children(sel)))
		return;
	if (arg->i > 0) {
		wl_list_for_each(c, &sel->link, link) {
			if (&c->link == &clients)
				continue; /* wrap past the sentinel node */
			if (VISIBLEON(c, selmon))
				break; /* found it */
		}
	} else {
		wl_list_for_each_reverse(c, &sel->link, link) {
			if (&c->link == &clients)
				continue; /* wrap past the sentinel node */
			if (VISIBLEON(c, selmon))
				break; /* found it */
		}
	}
	/* If only one client is visible on selmon, then c == sel */
	focusclient(c, 1);
}

/* We probably should change the name of this: it sounds like it
 * will focus the topmost client of this mon, when actually will
 * only return that client */
Client *
focustop(Monitor *m)
{
	Client *c;

	if (!m)
		return NULL;
	wl_list_for_each(c, &fstack, flink) {
		if (VISIBLEON(c, m))
			return c;
	}
	return NULL;
}

static void
focus_top(Monitor *m, int lift)
{
	Client *top = focustop(m);
	focusclient(top, lift);
}

/* ===== DIRECTIONAL FOCUS NAVIGATION (Cone Search) ===== */

/**
 * Get the center point of a window in world coordinates
 */
static void
window_center(Client *c, float *cx, float *cy)
{
	if (!c || !cx || !cy)
		return;
	
	/* Validate geometry to prevent division issues */
	if (c->geom.width <= 0 || c->geom.height <= 0)
		return;
	
	/* World coordinates are stored in c->world, but we need to account for
	 * the actual geometry (which includes the border) */
	if (c->world.set) {
		*cx = c->world.x + c->geom.width / 2.0f;
		*cy = c->world.y + c->geom.height / 2.0f;
	} else {
		/* Fallback to screen geometry if world coords not set */
		*cx = c->geom.x + c->geom.width / 2.0f;
		*cy = c->geom.y + c->geom.height / 2.0f;
	}
}

/**
 * Check if a point is within a cone defined by center angle and width
 * Returns the Euclidean distance if inside cone, -1.0f if outside
 */
static float
angle_distance_in_cone(float dx, float dy, float center_angle, float cone_width)
{
	float angle = atan2f(dy, dx);
	float diff = fabsf(atan2f(sinf(angle - center_angle), cosf(angle - center_angle)));
	
	if (diff <= cone_width / 2.0f) {
		return sqrtf(dx * dx + dy * dy);
	}
	return -1.0f;
}

/**
 * Core cone search algorithm - find nearest window in specified direction
 * angle: direction in radians (0 = right, PI/2 = down, PI = left, -PI/2 = up)
 * cone_width: width of search cone in radians
 * Returns nearest client or NULL if none found
 */
static Client *
cone_search_focus(float angle, float cone_width)
{
	Client *c, *nearest = NULL;
	Client *sel = focustop(selmon);
	float sel_cx, sel_cy;
	float min_dist = -1.0f;
	float c_cx, c_cy, dx, dy, dist;
	
	/* Get focus point - current window center or viewport center */
	if (sel && sel->world.set) {
		window_center(sel, &sel_cx, &sel_cy);
	} else {
		/* No focused window - use viewport center in world coordinates */
		Monitor *m = selmon;
		if (!m)
			return NULL;
		/* Viewport center in world coords - guard against zero zoom */
		float zoom = viewport.zoom > 0.0f ? viewport.zoom : 1.0f;
		sel_cx = viewport.x + m->w.width / (2.0f * zoom);
		sel_cy = viewport.y + m->w.height / (2.0f * zoom);
	}
	
	/* Search for nearest window within cone */
	wl_list_for_each(c, &clients, link) {
		/* Skip if not visible, floating, fullscreen, or is the current window */
		if (!VISIBLEON(c, selmon))
			continue;
		if (c->isfloating || c->isfullscreen)
			continue;
		if (c == sel)
			continue;
		if (!c->world.set)
			continue;
		
		window_center(c, &c_cx, &c_cy);
		
		/* Calculate vector from focus to target */
		dx = c_cx - sel_cx;
		dy = c_cy - sel_cy;
		
		/* Check if within cone and get distance */
		dist = angle_distance_in_cone(dx, dy, angle, cone_width);
		if (dist >= 0.0f && (min_dist < 0.0f || dist < min_dist)) {
			min_dist = dist;
			nearest = c;
		}
	}
	
	return nearest;
}

/**
 * Focus the nearest window in the specified direction
 * Uses cone search with 90° initial cone, widening to 180° if no window found
 */
void
focus_directional(const Arg *arg)
{
	Client *target = NULL;
	float angle;
	
	if (!selmon)
		return;
	
	/* Check if there are any clients to focus */
	if (wl_list_empty(&clients))
		return;
	
	/* Convert direction to angle in radians */
	switch (arg->i) {
	case DIR_LEFT:
		angle = M_PI;  /* 180 degrees */
		break;
	case DIR_RIGHT:
		angle = 0.0f;  /* 0 degrees */
		break;
	case DIR_UP:
		angle = -M_PI / 2.0f;  /* -90 degrees */
		break;
	case DIR_DOWN:
		angle = M_PI / 2.0f;   /* 90 degrees */
		break;
	default:
		return;
	}
	
	/* First try with 90° cone */
	target = cone_search_focus(angle, M_PI / 2.0f);
	
	/* If no window found, widen to 180° */
	if (!target) {
		target = cone_search_focus(angle, M_PI);
	}
	
	if (target) {
		wlr_log(WLR_DEBUG, "Focus directional: found window at (%.0f, %.0f)", 
			target->world.x, target->world.y);
		focusclient(target, 1);
	}
}

/* ===== END DIRECTIONAL FOCUS NAVIGATION ===== */

void
fullscreennotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, fullscreen);
	setfullscreen(c, client_wants_fullscreen(c));
}

void
gpureset(struct wl_listener *listener, void *data)
{
	struct wlr_renderer *old_drw = drw;
	struct wlr_allocator *old_alloc = alloc;
	struct Monitor *m;
	if (!(drw = wlr_renderer_autocreate(backend)))
		die("couldn't recreate renderer");

	if (!(alloc = wlr_allocator_autocreate(backend, drw)))
		die("couldn't recreate allocator");

	wl_list_remove(&gpu_reset.link);
	wl_signal_add(&drw->events.lost, &gpu_reset);

	wlr_compositor_set_renderer(compositor, drw);

	wl_list_for_each(m, &mons, link) {
		wlr_output_init_render(m->wlr_output, alloc, drw);
	}

	wlr_allocator_destroy(old_alloc);
	wlr_renderer_destroy(old_drw);
}

void
handlesig(int signo)
{
	if (signo == SIGCHLD)
		while (waitpid(-1, NULL, WNOHANG) > 0);
	else if (signo == SIGINT || signo == SIGTERM)
		quit(NULL);
}

void
inputdevice(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new input device becomes
	 * available. */
	struct wlr_input_device *device = data;
	uint32_t caps;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		createkeyboard(wlr_keyboard_from_input_device(device));
		break;
	case WLR_INPUT_DEVICE_POINTER:
		createpointer(wlr_pointer_from_input_device(device));
		break;
	default:
		/* TODO handle other input device types */
		break;
	}

	/* We need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. In dwl we always have a cursor, even if
	 * there are no pointer devices, so we always include that capability. */
	/* TODO do we actually require a cursor? */
	caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&kb_group->wlr_group->devices))
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	wlr_seat_set_capabilities(seat, caps);
}

int
keybinding(uint32_t mods, xkb_keysym_t sym)
{
	/*
	 * Here we handle compositor keybindings. This is when the compositor is
	 * processing keys, rather than passing them on to the client for its own
	 * processing.
	 */
	const Key *k;
	for (k = keys; k < END(keys); k++) {
		if (CLEANMASK(mods) == CLEANMASK(k->mod)
				&& xkb_keysym_to_lower(sym) == xkb_keysym_to_lower(k->keysym)
				&& k->func) {
			k->func(&k->arg);
			return 1;
		}
	}
	return 0;
}

void
keypress(struct wl_listener *listener, void *data)
{
	int i;
	/* This event is raised when a key is pressed or released. */
	KeyboardGroup *group = wl_container_of(listener, group, key);
	struct wlr_keyboard_key_event *event = data;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms;
	if (!group->wlr_group->keyboard.xkb_state)
		return;
	nsyms = xkb_state_key_get_syms(
			group->wlr_group->keyboard.xkb_state, keycode, &syms);

	int handled = 0;
	uint32_t mods = wlr_keyboard_get_modifiers(&group->wlr_group->keyboard);
	int is_super_key = 0;

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	for (i = 0; i < nsyms; i++) {
		if (syms[i] == XKB_KEY_Super_L || syms[i] == XKB_KEY_Super_R) {
			is_super_key = 1;
			break;
		}
	}

	/* Super-tap launcher: spawn on Super key *release* if it was a quick tap
	 * with no other key pressed while Super was held.
	 * This avoids interfering with MODKEY combos like Super+T.
	 */
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (is_super_key) {
			super_tap.down = 1;
			super_tap.consumed = 0;
			super_tap.press_time_msec = event->time_msec;
		} else if (super_tap.down) {
			super_tap.consumed = 1;
		}
	} else {
		if (is_super_key && super_tap.down) {
			uint32_t dt = event->time_msec - super_tap.press_time_msec;
			int should_spawn = !locked && !super_tap.consumed && dt <= SUPER_TAP_MAX_MS;
			super_tap.down = 0;
			super_tap.consumed = 0;
			if (should_spawn) {
				Arg a = {.v = menucmd};
				if (launcher_pid > 0 && kill(launcher_pid, 0) == 0) {
					/* kill entire process group (spawn_pid uses setsid()) */
					kill(-launcher_pid, SIGTERM);
					launcher_pid = -1;
				} else {
					launcher_pid = spawn_pid(&a);
				}
			}
		}
	}

	/* On _press_ if there is no active screen locker,
	 * attempt to process a compositor keybinding. */
	if (!locked && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (i = 0; i < nsyms; i++)
			handled = keybinding(mods, syms[i]) || handled;
	}

	if (handled && group->wlr_group->keyboard.repeat_info.delay > 0) {
		xkb_keysym_t *repeat_syms = NULL;
		if (nsyms > 0) {
			repeat_syms = ecalloc((size_t)nsyms, sizeof(*repeat_syms));
			memcpy(repeat_syms, syms, (size_t)nsyms * sizeof(*repeat_syms));
		}
		free(group->keysyms);
		group->keysyms = repeat_syms;
		group->mods = mods;
		group->nsyms = nsyms;
		wl_event_source_timer_update(group->key_repeat_source,
				group->wlr_group->keyboard.repeat_info.delay);
	} else {
		free(group->keysyms);
		group->keysyms = NULL;
		group->nsyms = 0;
		wl_event_source_timer_update(group->key_repeat_source, 0);
	}

	if (handled)
		return;

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	/* Pass unhandled keycodes along to the client. */
	wlr_seat_keyboard_notify_key(seat, event->time_msec,
			event->keycode, event->state);
}

void
keypressmod(struct wl_listener *listener, void *data)
{
	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	KeyboardGroup *group = wl_container_of(listener, group, modifiers);

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(seat,
			&group->wlr_group->keyboard.modifiers);
}

int
keyrepeat(void *data)
{
	KeyboardGroup *group = data;
	int i;
	if (!group->nsyms || !group->keysyms || group->wlr_group->keyboard.repeat_info.rate <= 0)
		return 0;

	wl_event_source_timer_update(group->key_repeat_source,
			1000 / group->wlr_group->keyboard.repeat_info.rate);

	for (i = 0; i < group->nsyms; i++)
		keybinding(group->mods, group->keysyms[i]);

	return 0;
}

void
killclient(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		client_send_close(sel);
}

void
locksession(struct wl_listener *listener, void *data)
{
	struct wlr_session_lock_v1 *session_lock = data;
	SessionLock *lock;
	wlr_scene_node_set_enabled(&locked_bg->node, 1);
	if (cur_lock) {
		wlr_session_lock_v1_destroy(session_lock);
		return;
	}
	lock = session_lock->data = ecalloc(1, sizeof(*lock));
	focusclient(NULL, 0);

	lock->scene = wlr_scene_tree_create(layers[LyrBlock]);
	cur_lock = lock->lock = session_lock;
	locked = 1;

	LISTEN(&session_lock->events.new_surface, &lock->new_surface, createlocksurface);
	LISTEN(&session_lock->events.destroy, &lock->destroy, destroysessionlock);
	LISTEN(&session_lock->events.unlock, &lock->unlock, unlocksession);

	wlr_session_lock_v1_send_locked(session_lock);
}

void
mapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is mapped, or ready to display on-screen. */
	Client *p = NULL;
	Client *w, *c = wl_container_of(listener, c, map);
	Monitor *m;
	int i;
	int restore_width;
	int restore_height;

	/* Create scene tree for this client and its border */
	c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LyrTile]);
	if (!c->scene) {
		client_surface(c)->data = NULL;
		wlr_log(WLR_ERROR, "Failed to create scene tree for mapped client");
		return;
	}
	/* Enabled later by a call to arrange() */
	wlr_scene_node_set_enabled(&c->scene->node, client_is_unmanaged(c));
	c->scene_surface = c->type == XDGShell
			? wlr_scene_xdg_surface_create(c->scene, c->surface.xdg)
			: wlr_scene_subsurface_tree_create(c->scene, client_surface(c));
	c->scene->node.data = c->scene_surface->node.data = c;
	
	/* DEBUG: createnotify scene_surface created */

	client_get_geometry(c, &c->geom);

	/* Handle unmanaged clients first so we can return prior create borders */
	if (client_is_unmanaged(c)) {
		/* Unmanaged clients always are floating */
		wlr_scene_node_reparent(&c->scene->node, layers[LyrFloat]);
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
		client_set_size(c, c->geom.width, c->geom.height);
		if (client_wants_focus(c)) {
			focusclient(c, 1);
			exclusive_focus = c;
		}
		goto unset_fullscreen;
	}

	for (i = 0; i < 4; i++) {
		c->border[i] = wlr_scene_rect_create(c->scene, 0, 0,
				c->isurgent ? urgentcolor : bordercolor);
		c->border[i]->node.data = c;
	}

	if (focusringpx > 0) {
		for (i = 0; i < 4; i++) {
			c->focus_ring[i] = wlr_scene_rect_create(c->scene, 0, 0, focuscolor);
			c->focus_ring[i]->node.data = c;
			wlr_scene_node_set_enabled(&c->focus_ring[i]->node, 0);
		}
	}

	/* Initialize client geometry with room for border */
	client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
	c->geom.width += 2 * c->bw;
	c->geom.height += 2 * c->bw;

	restore_width = 0;
	restore_height = 0;
	if (window_size_history_load(c, &restore_width, &restore_height)) {
		c->geom.width = MAX(1 + 2 * (int)c->bw, restore_width);
		c->geom.height = MAX(1 + 2 * (int)c->bw, restore_height);
		c->crop.active = false;
		c->crop.x = 0.0f;
		c->crop.y = 0.0f;
		c->crop.w = 1.0f;
		c->crop.h = 1.0f;
		c->crop.base_w = c->geom.width;
		c->crop.base_h = c->geom.height;
		c->crop.saved_base = true;
	}

	/* Insert this client into client lists. */
	wl_list_insert(&clients, &c->link);
	wl_list_insert(&fstack, &c->flink);

	/* Set initial monitor, floating status, and focus: clients that have a
	 * parent are always floating and inherit the parent's monitor.
	 * If there is no parent, apply rules */
	if ((p = client_get_parent(c))) {
		c->isfloating = 1;
		setmon(c, p->mon);
	} else {
		applyrules(c);
	}
	persistence_apply_client(c);
	ftl_create(c);
	printstatus();

unset_fullscreen:
	m = c->mon ? c->mon : xytomon(c->geom.x, c->geom.y);
	wl_list_for_each(w, &clients, link) {
		if (w != c && w != p && w->isfullscreen && m == w->mon)
			setfullscreen(w, 0);
	}
}

void
maximizenotify(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client would like to maximize itself,
	 * typically because the user clicked on the maximize button on
	 * client-side decorations. dwl doesn't support maximization, but
	 * to conform to xdg-shell protocol we still must send a configure.
	 * Since xdg-shell protocol v5 we should ignore request of unsupported
	 * capabilities, just schedule a empty configure when the client uses <5
	 * protocol version
	 * wlr_xdg_surface_schedule_configure() is used to send an empty reply. */
	Client *c = wl_container_of(listener, c, maximize);
	if (c->surface.xdg->initialized
			&& wl_resource_get_version(c->surface.xdg->toplevel->resource)
					< XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
		wlr_xdg_surface_schedule_configure(c->surface.xdg);
}

void
motionabsolute(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an _absolute_
	 * motion event, from 0..1 on each axis. This happens, for example, when
	 * wlroots is running under a Wayland window rather than KMS+DRM, and you
	 * move the mouse over the window. You could enter the window from any edge,
	 * so we have to warp the mouse there. Also, some hardware emits these events. */
	struct wlr_pointer_motion_absolute_event *event = data;
	double lx, ly, dx, dy;

	if (!event->time_msec) /* this is 0 with virtual pointers */
		wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);

	wlr_cursor_absolute_to_layout_coords(cursor, &event->pointer->base, event->x, event->y, &lx, &ly);
	dx = lx - cursor->x;
	dy = ly - cursor->y;
	motionnotify(event->time_msec, &event->pointer->base, dx, dy, dx, dy);
}

void
motionnotify(uint32_t time, struct wlr_input_device *device, double dx, double dy,
		double dx_unaccel, double dy_unaccel)
{
	double sx = 0, sy = 0, sx_confined, sy_confined;
	Client *c = NULL, *w = NULL;
	LayerSurface *l = NULL;
	struct wlr_surface *surface = NULL;
	struct wlr_pointer_constraint_v1 *constraint;

	/* Find the client under the pointer and send the event along. */
	xytonode(cursor->x, cursor->y, &surface, &c, NULL, &sx, &sy);

	if (cursor_mode == CurPressed && !seat->drag
			&& surface != seat->pointer_state.focused_surface
			&& toplevel_from_wlr_surface(seat->pointer_state.focused_surface, &w, &l) >= 0) {
		c = w;
		surface = seat->pointer_state.focused_surface;
		sx = cursor->x - (l ? l->scene->node.x : w->geom.x);
		sy = cursor->y - (l ? l->scene->node.y : w->geom.y);
	}

	/* time is 0 in internal calls meant to restore pointer focus. */
	if (time) {
		wlr_relative_pointer_manager_v1_send_relative_motion(
				relative_pointer_mgr, seat, (uint64_t)time * 1000,
				dx, dy, dx_unaccel, dy_unaccel);

		wl_list_for_each(constraint, &pointer_constraints->constraints, link)
			cursorconstrain(constraint);

		if (active_constraint && cursor_mode != CurResize && cursor_mode != CurMove) {
			toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
			if (c && active_constraint->surface == seat->pointer_state.focused_surface) {
				sx = cursor->x - c->geom.x - c->bw;
				sy = cursor->y - c->geom.y - c->bw;
				if (wlr_region_confine(&active_constraint->region, sx, sy,
						sx + dx, sy + dy, &sx_confined, &sy_confined)) {
					dx = sx_confined - sx;
					dy = sy_confined - sy;
				}

				if (active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
					return;
			}
		}

		wlr_cursor_move(cursor, device, dx, dy);
		wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

		/* Update selmon (even while dragging a window) */
		if (sloppyfocus)
			selmon = xytomon(cursor->x, cursor->y);
	}

	/* Update drag icon's position */
	wlr_scene_node_set_position(&drag_icon->node, (int)round(cursor->x), (int)round(cursor->y));

	if ((cursor_mode == CurMove || cursor_mode == CurResize) && !grabc) {
		cursor_mode = CurNormal;
		return;
	}

	/* If we are currently grabbing the mouse, handle and return.
	 * geom is in world space; the cursor is in screen space, so convert via
	 * SCREEN_TO_WORLD (which accounts for pan + zoom). grabcx/grabcy hold the
	 * grab offset in world units (see moveresize()). */
	if (cursor_mode == CurMove && grabc) {
		/* Move the grabbed client to the new position. */
		resize(grabc, (struct wlr_box){
			.x = (int)lroundf(SCREEN_TO_WORLD_X(cursor->x)) - grabcx,
			.y = (int)lroundf(SCREEN_TO_WORLD_Y(cursor->y)) - grabcy,
			.width = grabc->geom.width, .height = grabc->geom.height}, 1);
		return;
	} else if (cursor_mode == CurResize && grabc) {
		resize(grabc, (struct wlr_box){.x = grabc->geom.x, .y = grabc->geom.y,
			.width = (int)lroundf(SCREEN_TO_WORLD_X(cursor->x)) - grabc->geom.x,
			.height = (int)lroundf(SCREEN_TO_WORLD_Y(cursor->y)) - grabc->geom.y}, 1);
		return;
	}
	
	/* Crop mode: update selection rectangle */
	if (crop_editor.active && crop_editor.dragging) {
		crop_editor.end_x = cursor->x;
		crop_editor.end_y = cursor->y;
		cropdraw();
		return;
	}

	/* If there's no client surface under the cursor, set the cursor image to a
	 * default. This is what makes the cursor image appear when you move it
	 * off of a client or over its border. */
	if (!surface && !seat->drag)
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	pointerfocus(c, surface, sx, sy, time);
}

void
motionrelative(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	struct wlr_pointer_motion_event *event = data;
	/* The cursor doesn't move unless we tell it to. The cursor automatically
	 * handles constraining the motion to the output layout, as well as any
	 * special configuration applied for the specific input device which
	 * generated the event. You can pass NULL for the device if you want to move
	 * the cursor around without any input. */
	motionnotify(event->time_msec, &event->pointer->base, event->delta_x, event->delta_y,
			event->unaccel_dx, event->unaccel_dy);
}

void
moveresize(const Arg *arg)
{
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	xytonode(cursor->x, cursor->y, NULL, &grabc, NULL, NULL, NULL);
	if (!grabc || client_is_unmanaged(grabc) || grabc->isfullscreen)
		return;

	/* Float the window and tell motionnotify to grab it */
	setfloating(grabc, 1);
	switch (cursor_mode = arg->ui) {
	case CurMove:
		/* Offset stored in world units (cursor is screen space). */
		grabcx = (int)lroundf(SCREEN_TO_WORLD_X(cursor->x)) - grabc->geom.x;
		grabcy = (int)lroundf(SCREEN_TO_WORLD_Y(cursor->y)) - grabc->geom.y;
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "all-scroll");
		break;
	case CurResize:
		/* Doesn't work for X11 output - the next absolute motion event
		 * returns the cursor to where it started. Warp target is screen space. */
		wlr_cursor_warp_closest(cursor, NULL,
				WORLD_TO_SCREEN_X(grabc->geom.x + grabc->geom.width),
				WORLD_TO_SCREEN_Y(grabc->geom.y + grabc->geom.height));
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "se-resize");
		break;
	}
}

void
outputmgrapply(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, 0);
}

void
outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test)
{
	/*
	 * Called when a client such as wlr-randr requests a change in output
	 * configuration. This is only one way that the layout can be changed,
	 * so any Monitor information should be updated by updatemons() after an
	 * output_layout.change event, not here.
	 */
	struct wlr_output_configuration_head_v1 *config_head;
	int ok = 1;

	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;
		Monitor *m = wlr_output->data;
		struct wlr_output_state state;

		/* Ensure displays previously disabled by wlr-output-power-management-v1
		 * are properly handled*/
		m->asleep = 0;

		wlr_output_state_init(&state);
		wlr_output_state_set_enabled(&state, config_head->state.enabled);
		if (!config_head->state.enabled)
			goto apply_or_test;

		if (config_head->state.mode)
			wlr_output_state_set_mode(&state, config_head->state.mode);
		else
			wlr_output_state_set_custom_mode(&state,
					config_head->state.custom_mode.width,
					config_head->state.custom_mode.height,
					config_head->state.custom_mode.refresh);

		wlr_output_state_set_transform(&state, config_head->state.transform);
		wlr_output_state_set_scale(&state, config_head->state.scale);
		wlr_output_state_set_adaptive_sync_enabled(&state,
				config_head->state.adaptive_sync_enabled);

apply_or_test:
		ok &= test ? wlr_output_test_state(wlr_output, &state)
				: wlr_output_commit_state(wlr_output, &state);

		/* Don't move monitors if position wouldn't change. This avoids
		 * wlroots marking the output as manually configured.
		 * wlr_output_layout_add does not like disabled outputs */
		if (!test && wlr_output->enabled && (m->m.x != config_head->state.x || m->m.y != config_head->state.y))
			wlr_output_layout_add(output_layout, wlr_output,
					config_head->state.x, config_head->state.y);

		wlr_output_state_finish(&state);
	}

	if (ok)
		wlr_output_configuration_v1_send_succeeded(config);
	else
		wlr_output_configuration_v1_send_failed(config);
	wlr_output_configuration_v1_destroy(config);

	/* https://codeberg.org/dwl/dwl/issues/577 */
	updatemons(NULL, NULL);
}

void
outputmgrtest(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, 1);
}

void
pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy,
		uint32_t time)
{
	struct timespec now;

	if (surface != seat->pointer_state.focused_surface &&
			sloppyfocus && time && c && !client_is_unmanaged(c))
		focusclient(c, 0);

	/* If surface is NULL, clear pointer focus */
	if (!surface) {
		wlr_seat_pointer_notify_clear_focus(seat);
		return;
	}

	if (!time) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
	}

	/* Let the client know that the mouse cursor has entered one
	 * of its surfaces, and make keyboard focus follow if desired.
	 * wlroots makes this a no-op if surface is already focused */
	wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}

void
printstatus(void)
{
	Monitor *m = NULL;
	Client *c;

	wl_list_for_each(m, &mons, link) {
		if ((c = focustop(m))) {
			printf("%s title %s\n", m->wlr_output->name, client_get_title(c));
			printf("%s appid %s\n", m->wlr_output->name, client_get_appid(c));
			printf("%s fullscreen %d\n", m->wlr_output->name, c->isfullscreen);
			printf("%s floating %d\n", m->wlr_output->name, c->isfloating);
		} else {
			printf("%s title \n", m->wlr_output->name);
			printf("%s appid \n", m->wlr_output->name);
			printf("%s fullscreen \n", m->wlr_output->name);
			printf("%s floating \n", m->wlr_output->name);
		}

		printf("%s selmon %u\n", m->wlr_output->name, m == selmon);
		printf("%s layout %s\n", m->wlr_output->name, m->ltsymbol);

		wl_list_for_each(c, &clients, link) {
			const char *title;
			const char *appid;
			if (c->mon != m)
				continue;
			title = client_get_title(c);
			appid = client_get_appid(c);
			if (!title)
				title = "";
			if (!appid)
				appid = "";
			printf("%s win %s %s world %.0f %.0f set %d geom %d %d %d %d\n",
				m->wlr_output->name, appid, title,
				c->world.x, c->world.y, c->world.set,
				c->geom.x, c->geom.y, c->geom.width, c->geom.height);
		}
	}
	
	/* Output viewport state for debugging and status bars */
	printf("viewport %.0f %.0f %.2f follow %s follow_new %s\n",
		viewport.x, viewport.y, viewport.zoom,
		viewport.follow ? "on" : "off",
		viewport.follow_new_windows ? "on" : "off");
	
	/* Output crop mode state */
	printf("crop %s\n", crop_editor.active ? "active" : "inactive");

	fflush(stdout);

	/* Mirror focus/fullscreen state to foreign-toplevel handles for shells. */
	ftl_sync_state();

	/* Push viewport/compositor state to any connected IPC shell clients. */
	ipc_broadcast_state();
}

void
powermgrsetmode(struct wl_listener *listener, void *data)
{
	struct wlr_output_power_v1_set_mode_event *event = data;
	struct wlr_output_state state = {0};
	Monitor *m = event->output->data;

	if (!m)
		return;

	m->gamma_lut_changed = 1; /* Reapply gamma LUT when re-enabling the output */
	wlr_output_state_set_enabled(&state, event->mode);
	wlr_output_commit_state(m->wlr_output, &state);

	m->asleep = !event->mode;
	updatemons(NULL, NULL);
}

void
quit(const Arg *arg)
{
	time_t now = time(NULL);
	
	if (!exit_confirm.pending || 
	    (now - exit_confirm.last_press) > EXIT_CONFIRMATION_SECONDS) {
		/* First press or timer expired - show confirmation message */
		exit_confirm.pending = 1;
		exit_confirm.last_press = now;
		wlr_log(WLR_INFO, "Press exit key again within %d seconds to quit", 
			EXIT_CONFIRMATION_SECONDS);
		/* Also print to stdout for status bars */
		printf("exit_confirm pending %d\n", EXIT_CONFIRMATION_SECONDS);
		fflush(stdout);
	} else {
		/* Second press within timeout - actually exit */
		wlr_log(WLR_INFO, "Exit confirmed - quitting dwl");
		wl_display_terminate(dpy);
	}
}

#include "modules/crop/crop_mode.c"
#include "modules/layout/layout_world.c"
#include "modules/ui/offscreen_indicators.c"
#include "modules/ui/overlay_clock.c"
#include "modules/ui/wallpaper.c"
#include "modules/viewport/viewport_ops.c"
#include "modules/ipc.c"

void
rendermon(struct wl_listener *listener, void *data)
{
	/* This function is called every time an output is ready to display a frame,
	 * generally at the output's refresh rate (e.g. 60Hz). */
	Monitor *m = wl_container_of(listener, m, frame);
	Client *c;
	struct wlr_output_state pending = {0};
	struct timespec now;

	viewport_tick();
	offscreen_indicators_update();

	/* Render if no XDG clients have an outstanding resize and are visible on
	 * this monitor. */
	wl_list_for_each(c, &clients, link) {
		if (c->resize && !c->isfloating && client_is_rendered_on_mon(c, m) && !client_is_stopped(c))
			goto skip;
	}

	wlr_scene_output_commit(m->scene_output, NULL);

skip:
	/* Let clients know a frame has been rendered */
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(m->scene_output, &now);
	wlr_output_state_finish(&pending);
}

void
requestdecorationmode(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_decoration_mode);
	if (c->surface.xdg->initialized)
		wlr_xdg_toplevel_decoration_v1_set_mode(c->decoration,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void
requeststartdrag(struct wl_listener *listener, void *data)
{
	struct wlr_seat_request_start_drag_event *event = data;

	if (wlr_seat_validate_pointer_grab_serial(seat, event->origin,
			event->serial))
		wlr_seat_start_pointer_drag(seat, event->drag, event->serial);
	else
		wlr_data_source_destroy(event->drag->source);
}

void
compositor_resize_client(Client *c, const struct wlr_box *geo, int interact)
{
	if (!geo)
		return;
	resize(c, *geo, interact);
}

void
requestmonstate(struct wl_listener *listener, void *data)
{
	struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(event->output, event->state);
	updatemons(NULL, NULL);
}

void
resize(Client *c, struct wlr_box geo, int interact)
{
	struct wlr_box *bbox;
	struct wlr_box clip;
	int cfg_w, cfg_h;
	int view_x, view_y;
	int unbounded_infinite_tile;
	int base_inner_w, base_inner_h;
	int src_x, src_y, vis_w, vis_h;
	int full_clip_w, full_clip_h;
	
	/* DEBUG: resize start */

	if (!c || !c->mon || !client_surface(c)->mapped || !c->scene || !c->scene_surface) {
		/* DEBUG: resize early return - null checks failed */
		return;
	}
	
	/* Borders may not be created yet during early setup */
	if (!c->border[0] || !c->border[1] || !c->border[2] || !c->border[3]) {
		/* DEBUG: resize early return - missing borders */
		return;
	}

	bbox = interact ? &sgeom : &c->mon->w;
	unbounded_infinite_tile = !interact
		&& c->mon
		&& c->mon->lt[c->mon->sellt]->arrange == infinite
		&& !c->isfloating
		&& !c->isfullscreen;

	client_set_bounds(c, geo.width, geo.height);
	c->geom = geo;

	/* In infinite layout, tiled clients live in world space and may extend
	 * beyond the monitor viewport. Keep minimum size constraints, but don't
	 * clamp their x/y to monitor bounds. */
	if (unbounded_infinite_tile) {
		c->geom.width = MAX(1 + 2 * (int)c->bw, c->geom.width);
		c->geom.height = MAX(1 + 2 * (int)c->bw, c->geom.height);
	} else {
		applybounds(c, bbox);
	}

	if (!c->crop.active) {
		c->crop.base_w = c->geom.width;
		c->crop.base_h = c->geom.height;
		c->crop.saved_base = true;
	}

	/* Apply viewport transform: world -> screen coordinates (includes zoom). */
	view_x = WORLD_TO_SCREEN_X(c->geom.x);
	view_y = WORLD_TO_SCREEN_Y(c->geom.y);

	/* The whole window (frame + content) is displayed at geom * zoom. The client
	 * stays configured at native size (cfg_w/cfg_h below) — only the displayed
	 * size scales, via these scaled frame dims and the buffer dest_size. At
	 * zoom == 1 every value below reduces to the unscaled original. */
	{
		float zf = viewport.zoom > 0.0f ? viewport.zoom : 1.0f;
		int z_bw = (int)lroundf(c->bw * zf);
		int z_w  = MAX(1, (int)lroundf(c->geom.width * zf));
		int z_h  = MAX(1, (int)lroundf(c->geom.height * zf));
		int z_inner_h = MAX(0, z_h - 2 * z_bw);

		/* Update scene-graph frame */
		wlr_scene_node_set_position(&c->scene->node, view_x, view_y);
		wlr_scene_node_set_position(&c->border[0]->node, 0, 0);
		wlr_scene_rect_set_size(c->border[0], z_w, z_bw);
		wlr_scene_rect_set_size(c->border[1], z_w, z_bw);
		wlr_scene_rect_set_size(c->border[2], z_bw, z_inner_h);
		wlr_scene_rect_set_size(c->border[3], z_bw, z_inner_h);
		wlr_scene_node_set_position(&c->border[1]->node, 0, z_h - z_bw);
		wlr_scene_node_set_position(&c->border[2]->node, 0, z_bw);
		wlr_scene_node_set_position(&c->border[3]->node, z_w - z_bw, z_bw);

		if (focusringpx > 0 && c->focus_ring[0]) {
			int ring = MAX(1, (int)lroundf(focusringpx * zf));
			int ring_w = z_w + 2 * ring;
			int ring_h = z_h + 2 * ring;
			wlr_scene_rect_set_size(c->focus_ring[0], ring_w, ring);
			wlr_scene_rect_set_size(c->focus_ring[1], ring_w, ring);
			wlr_scene_rect_set_size(c->focus_ring[2], ring, ring_h);
			wlr_scene_rect_set_size(c->focus_ring[3], ring, ring_h);
			wlr_scene_node_set_position(&c->focus_ring[0]->node, -ring, -ring);
			wlr_scene_node_set_position(&c->focus_ring[1]->node, -ring, z_h);
			wlr_scene_node_set_position(&c->focus_ring[2]->node, -ring, -ring);
			wlr_scene_node_set_position(&c->focus_ring[3]->node, z_w, -ring);
		}

		client_get_clip(c, &clip);

		/* True crop: keep client configured at base size, but show only a
		 * cropped region using scene-surface offset + clip rectangle. */
		if (c->crop.active && c->crop.saved_base
				&& c->crop.base_w > 2 * (int)c->bw && c->crop.base_h > 2 * (int)c->bw) {
			base_inner_w = c->crop.base_w - 2 * (int)c->bw;
			base_inner_h = c->crop.base_h - 2 * (int)c->bw;
			full_clip_w = base_inner_w;
			full_clip_h = base_inner_h;
			if (clip.x > 0)
				full_clip_w = MAX(1, base_inner_w - clip.x);
			if (clip.y > 0)
				full_clip_h = MAX(1, base_inner_h - clip.y);

			src_x = (int)lroundf(c->crop.x * base_inner_w);
			src_y = (int)lroundf(c->crop.y * base_inner_h);
			vis_w = (int)lroundf(c->crop.w * base_inner_w);
			vis_h = (int)lroundf(c->crop.h * base_inner_h);

			src_x = MAX(0, MIN(full_clip_w - 1, src_x));
			src_y = MAX(0, MIN(full_clip_h - 1, src_y));
			vis_w = MAX(1, MIN(full_clip_w - src_x, vis_w));
			vis_h = MAX(1, MIN(full_clip_h - src_y, vis_h));

			/* Shift content so selected source region starts at frame origin
			 * (offset scaled to screen space). */
			wlr_scene_node_set_position(&c->scene_surface->node,
					z_bw - (int)lroundf(src_x * zf), z_bw - (int)lroundf(src_y * zf));

			/* Clip subtree to selected source region (source/buffer coords). */
			clip.x += src_x;
			clip.y += src_y;
			clip.width = vis_w;
			clip.height = vis_h;

			cfg_w = base_inner_w;
			cfg_h = base_inner_h;
		} else {
			wlr_scene_node_set_position(&c->scene_surface->node, z_bw, z_bw);
			cfg_w = c->geom.width - 2 * (int)c->bw;
			cfg_h = c->geom.height - 2 * (int)c->bw;
		}
	}

	cfg_w = MAX(1, cfg_w);
	cfg_h = MAX(1, cfg_h);

	/* this is a no-op if size hasn't changed */
	c->resize = client_set_size(c, cfg_w, cfg_h);
	wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);

	/* Scale the displayed buffer to match the zoomed frame. */
	client_set_buffer_scale(c, viewport.zoom);
}

/* Helper to check if a float is close to an integer */
static int
is_integer_zoom(float zoom)
{
	float rounded = roundf(zoom);
	return fabsf(zoom - rounded) < 0.01f;
}

/* Scale the window's buffer to implement zoom at the content level
 * This makes window content larger/smaller rather than just moving windows apart */
void
client_set_buffer_scale(Client *c, float scale)
{
	struct wlr_scene_node *node, *tmp;
	struct wlr_scene_buffer *buffer;
	int dest_w, dest_h;
	
	if (!c || !c->scene_surface)
		return;
	if (scale <= 0.0f)
		scale = 1.0f;
	
	/* Validate geometry */
	if (c->geom.width <= 2 * (int)c->bw || c->geom.height <= 2 * (int)c->bw)
		return;
	
	/* Calculate destination size */
	dest_w = (int)((c->geom.width - 2 * c->bw) * scale);
	dest_h = (int)((c->geom.height - 2 * c->bw) * scale);

	if (dest_w < 1)
		dest_w = 1;
	if (dest_h < 1)
		dest_h = 1;
	
	/* Find the buffer node inside scene_surface tree.
	 * node->parent is the wlr_scene_tree that owns the node, so compare it
	 * against the tree itself (not its embedded node). The previous form
	 * compared a wlr_scene_tree* to a wlr_scene_node*, which was always
	 * unequal and skipped every child, disabling buffer scaling. */
	wl_list_for_each_safe(node, tmp, &c->scene_surface->children, link) {
		if (node->parent != c->scene_surface)
			continue;
		if (node->type != WLR_SCENE_NODE_BUFFER)
			continue;
		buffer = wlr_scene_buffer_from_node(node);
		if (!buffer)
			continue;

		wlr_scene_buffer_set_dest_size(buffer, dest_w, dest_h);

		/* Choose filter based on scale type */
		if (is_integer_zoom(scale)) {
			wlr_scene_buffer_set_filter_mode(buffer, WLR_SCALE_FILTER_NEAREST);
		} else {
			wlr_scene_buffer_set_filter_mode(buffer, WLR_SCALE_FILTER_BILINEAR);
		}
	}
}

void
run(char *startup_cmd)
{
	/* Add a Unix socket to the Wayland display. */
	const char *socket = wl_display_add_socket_auto(dpy);
	if (!socket)
		die("startup: display_add_socket_auto");
	setenv("WAYLAND_DISPLAY", socket, 1);
	ipc_init(socket);

	/* Start the backend. This will enumerate outputs and inputs, become the DRM
	 * master, etc */
	if (!wlr_backend_start(backend))
		die("startup: backend_start");

	/* Now that the socket exists and the backend is started, run the startup command */
	if (startup_cmd) {
		int piperw[2];
		if (pipe(piperw) < 0)
			die("startup: pipe:");
		if ((child_pid = fork()) < 0)
			die("startup: fork:");
		if (child_pid == 0) {
			setsid();
			dup2(piperw[0], STDIN_FILENO);
			close(piperw[0]);
			close(piperw[1]);
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
			die("startup: execl:");
		}
		dup2(piperw[1], STDOUT_FILENO);
		close(piperw[1]);
		close(piperw[0]);
	}

	/* Mark stdout as non-blocking to avoid the startup script
	 * causing dwl to freeze when a user neither closes stdin
	 * nor consumes standard input in his startup script */

	if (fd_set_nonblock(STDOUT_FILENO) < 0)
		close(STDOUT_FILENO);

	printstatus();

	/* At this point the outputs are initialized, choose initial selmon based on
	 * cursor position, and set default cursor image */
	selmon = xytomon(cursor->x, cursor->y);

	/* TODO hack to get cursor to display in its initial location (100, 100)
	 * instead of (0, 0) and then jumping. Still may not be fully
	 * initialized, as the image/coordinates are not transformed for the
	 * monitor when displayed here */
	wlr_cursor_warp_closest(cursor, NULL, cursor->x, cursor->y);
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	/* Run the Wayland event loop. This does not return until you exit the
	 * compositor. Starting the backend rigged up all of the necessary event
	 * loop configuration to listen to libinput events, DRM events, generate
	 * frame events at the refresh rate, and so on. */
	wl_display_run(dpy);
}

void
setcursor(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	/* If we're "grabbing" the cursor, don't use the client's image, we will
	 * restore it after "grabbing" sending a leave event, followed by a enter
	 * event, which will result in the client requesting set the cursor surface */
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	/* This can be sent by any client, so we check to make sure this one
	 * actually has pointer focus first. If so, we can tell the cursor to
	 * use the provided surface as the cursor image. It will set the
	 * hardware cursor on the output that it's currently on and continue to
	 * do so as the cursor moves between outputs. */
	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_surface(cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
}

void
setcursorshape(struct wl_listener *listener, void *data)
{
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	/* This can be sent by any client, so we check to make sure this one
	 * actually has pointer focus first. If so, we can tell the cursor to
	 * use the provided cursor shape. */
	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_xcursor(cursor, cursor_mgr,
				wlr_cursor_shape_v1_name(event->shape));
}

static void
opacity_iter(struct wlr_scene_buffer *buffer, int sx, int sy, void *data)
{
	wlr_scene_buffer_set_opacity(buffer, *(float *)data);
}

/* Apply the client's current opacity to every scene buffer in its subtree.
 * Called on change and on each commit so new sub/popup buffers inherit it. */
void
applyopacity(Client *c)
{
	if (c && c->scene)
		wlr_scene_node_for_each_buffer(&c->scene->node, opacity_iter, &c->opacity);
}

void
setopacity(Client *c, float opacity)
{
	if (!c)
		return;
	c->opacity = fmaxf(0.1f, fminf(1.0f, opacity));
	applyopacity(c);
}

/* Keybind: nudge the focused window's opacity by arg->f. */
void
opacityadjust(const Arg *arg)
{
	Client *c = focustop(selmon);
	if (c && arg)
		setopacity(c, c->opacity + arg->f);
}

void
setfloating(Client *c, int floating)
{
	Client *p = client_get_parent(c);
	c->isfloating = floating;
	/* If in floating layout do not change the client's layer */
	if (!c->mon || !client_surface(c)->mapped || !c->mon->lt[c->mon->sellt]->arrange)
		return;
	wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen ||
			(p && p->isfullscreen) ? LyrFS
			: c->isfloating ? LyrFloat : LyrTile]);
	arrange(c->mon);
	printstatus();
}

void
setfullscreen(Client *c, int fullscreen)
{
	c->isfullscreen = fullscreen;
	if (!c->mon || !client_surface(c)->mapped)
		return;
	c->bw = fullscreen ? 0 : borderpx;
	client_set_fullscreen(c, fullscreen);
	wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen
			? LyrFS : c->isfloating ? LyrFloat : LyrTile]);

	if (fullscreen) {
		c->prev = c->geom;
		resize(c, c->mon->m, 0);
	} else {
		/* restore previous size instead of arrange for floating windows since
		 * client positions are set by the user and cannot be recalculated */
		resize(c, c->prev, 0);
	}
	arrange(c->mon);
	printstatus();
}

void
setlayout(const Arg *arg)
{
	if (!selmon)
		return;
	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
		selmon->sellt ^= 1;
	if (arg && arg->v)
		selmon->lt[selmon->sellt] = (Layout *)arg->v;
	snprintf(selmon->ltsymbol, sizeof(selmon->ltsymbol), "%s", selmon->lt[selmon->sellt]->symbol);
	arrange(selmon);
	printstatus();
}

void
setmon(Client *c, Monitor *m)
{
	Monitor *oldmon = c->mon;

	if (oldmon == m)
		return;
	c->mon = m;
	c->prev = c->geom;

	/* Scene graph sends surface leave/enter events on move and resize */
	if (oldmon)
		arrange(oldmon);
	if (m) {
		/* Make sure window actually overlaps with the monitor */
		resize(c, c->geom, 0);
		setfullscreen(c, c->isfullscreen); /* This will call arrange(c->mon) */
		setfloating(c, c->isfloating);
	}
	focus_top(selmon, 1);
}

void
setpsel(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in dwl we always honor them
	 */
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

void
setsel(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in dwl we always honor them
	 */
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(seat, event->source, event->serial);
}

void
setup(void)
{
	int drm_fd, i, sig[] = {SIGCHLD, SIGINT, SIGTERM, SIGPIPE};
	struct sigaction sa = {.sa_flags = SA_RESTART, .sa_handler = handlesig};
	sigemptyset(&sa.sa_mask);

	for (i = 0; i < (int)LENGTH(sig); i++)
		sigaction(sig[i], &sa, NULL);

	wlr_log_init(log_level, NULL);

	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, managing Wayland globals, and so on. */
	dpy = wl_display_create();
	event_loop = wl_display_get_event_loop(dpy);

	/* The backend is a wlroots feature which abstracts the underlying input and
	 * output hardware. The autocreate option will choose the most suitable
	 * backend based on the current environment, such as opening an X11 window
	 * if an X11 server is running. */
	if (!(backend = wlr_backend_autocreate(event_loop, &session)))
		die("couldn't create backend");

	/* Initialize the scene graph used to lay out windows */
	scene = wlr_scene_create();
	root_bg = wlr_scene_rect_create(&scene->tree, 0, 0, rootcolor);
	for (i = 0; i < NUM_LAYERS; i++)
		layers[i] = wlr_scene_tree_create(&scene->tree);
	drag_icon = wlr_scene_tree_create(&scene->tree);
	wlr_scene_node_place_below(&drag_icon->node, &layers[LyrBlock]->node);
	overlay_clock_init();
	offscreen_indicators_init();

	/* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
	 * can also specify a renderer using the WLR_RENDERER env var.
	 * The renderer is responsible for defining the various pixel formats it
	 * supports for shared memory, this configures that for clients. */
	if (!(drw = wlr_renderer_autocreate(backend)))
		die("couldn't create renderer");
	wl_signal_add(&drw->events.lost, &gpu_reset);

	/* Create shm, drm and linux_dmabuf interfaces by ourselves.
	 * The simplest way is to call:
	 *      wlr_renderer_init_wl_display(drw);
	 * but we need to create the linux_dmabuf interface manually to integrate it
	 * with wlr_scene. */
	wlr_renderer_init_wl_shm(drw, dpy);

	if (wlr_renderer_get_texture_formats(drw, WLR_BUFFER_CAP_DMABUF)) {
		wlr_drm_create(dpy, drw);
		wlr_scene_set_linux_dmabuf_v1(scene,
				wlr_linux_dmabuf_v1_create_with_renderer(dpy, 5, drw));
	}

	if ((drm_fd = wlr_renderer_get_drm_fd(drw)) >= 0 && drw->features.timeline
			&& backend->features.timeline)
		wlr_linux_drm_syncobj_manager_v1_create(dpy, 1, drm_fd);

	/* Autocreates an allocator for us.
	 * The allocator is the bridge between the renderer and the backend. It
	 * handles the buffer creation, allowing wlroots to render onto the
	 * screen */
	if (!(alloc = wlr_allocator_autocreate(backend, drw)))
		die("couldn't create allocator");

	/* This creates some hands-off wlroots interfaces. The compositor is
	 * necessary for clients to allocate surfaces and the data device manager
	 * handles the clipboard. Each of these wlroots interfaces has room for you
	 * to dig your fingers in and play with their behavior if you want. Note that
	 * the clients cannot set the selection directly without compositor approval,
	 * see the setsel() function. */
	compositor = wlr_compositor_create(dpy, 6, drw);
	wlr_subcompositor_create(dpy);
	wlr_data_device_manager_create(dpy);
	wlr_export_dmabuf_manager_v1_create(dpy);
	wlr_screencopy_manager_v1_create(dpy);
	foreign_toplevel_mgr = wlr_foreign_toplevel_manager_v1_create(dpy);
	wlr_data_control_manager_v1_create(dpy);
	wlr_ext_data_control_manager_v1_create(dpy, 1);
	wlr_primary_selection_v1_device_manager_create(dpy);
	wlr_viewporter_create(dpy);
	wlr_single_pixel_buffer_manager_v1_create(dpy);
	wlr_fractional_scale_manager_v1_create(dpy, 1);
	wlr_presentation_create(dpy, backend, 2);
	wlr_alpha_modifier_v1_create(dpy);

	/* Initializes the interface used to implement urgency hints */
	activation = wlr_xdg_activation_v1_create(dpy);
	wl_signal_add(&activation->events.request_activate, &request_activate);

	wlr_scene_set_gamma_control_manager_v1(scene, wlr_gamma_control_manager_v1_create(dpy));

	power_mgr = wlr_output_power_manager_v1_create(dpy);
	wl_signal_add(&power_mgr->events.set_mode, &output_power_mgr_set_mode);

	/* Creates an output layout, which is a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	output_layout = wlr_output_layout_create(dpy);
	wl_signal_add(&output_layout->events.change, &layout_change);

    wlr_xdg_output_manager_v1_create(dpy, output_layout);

	/* Configure a listener to be notified when new outputs are available on the
	 * backend. */
	wl_list_init(&mons);
	wl_signal_add(&backend->events.new_output, &new_output);

	/* Set up our client lists, the xdg-shell and the layer-shell. The xdg-shell is a
	 * Wayland protocol which is used for application windows. For more
	 * detail on shells, refer to the article:
	 *
	 * https://drewdevault.com/2018/07/29/Wayland-shells.html
	 */
	wl_list_init(&clients);
	wl_list_init(&fstack);
	wl_list_init(&static_listeners);

	xdg_shell = wlr_xdg_shell_create(dpy, 6);
	wl_signal_add(&xdg_shell->events.new_toplevel, &new_xdg_toplevel);
	wl_signal_add(&xdg_shell->events.new_popup, &new_xdg_popup);

	layer_shell = wlr_layer_shell_v1_create(dpy, 3);
	wl_signal_add(&layer_shell->events.new_surface, &new_layer_surface);

	idle_notifier = wlr_idle_notifier_v1_create(dpy);

	idle_inhibit_mgr = wlr_idle_inhibit_v1_create(dpy);
	wl_signal_add(&idle_inhibit_mgr->events.new_inhibitor, &new_idle_inhibitor);

	session_lock_mgr = wlr_session_lock_manager_v1_create(dpy);
	wl_signal_add(&session_lock_mgr->events.new_lock, &new_session_lock);
	locked_bg = wlr_scene_rect_create(layers[LyrBlock], sgeom.width, sgeom.height,
			(float [4]){0.1f, 0.1f, 0.1f, 1.0f});
	wlr_scene_node_set_enabled(&locked_bg->node, 0);

	/* Use decoration protocols to negotiate server-side decorations */
	wlr_server_decoration_manager_set_default_mode(
			wlr_server_decoration_manager_create(dpy),
			WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
	xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(dpy);
	wl_signal_add(&xdg_decoration_mgr->events.new_toplevel_decoration, &new_xdg_decoration);

	pointer_constraints = wlr_pointer_constraints_v1_create(dpy);
	wl_signal_add(&pointer_constraints->events.new_constraint, &new_pointer_constraint);

	relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(dpy);

	/*
	 * Creates a cursor, which is a wlroots utility for tracking the cursor
	 * image shown on screen.
	 */
	cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(cursor, output_layout);

	/* Creates an xcursor manager, another wlroots utility which loads up
	 * Xcursor themes to source cursor images from and makes sure that cursor
	 * images are available at all scale factors on the screen (necessary for
	 * HiDPI support). Scaled cursors will be loaded with each output. */
	cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	setenv("XCURSOR_SIZE", "24", 1);

	/*
	 * wlr_cursor *only* displays an image on screen. It does not move around
	 * when the pointer moves. However, we can attach input devices to it, and
	 * it will generate aggregate events for all of them. In these events, we
	 * can choose how we want to process them, forwarding them to clients and
	 * moving the cursor around. More detail on this process is described in
	 * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html
	 *
	 * And more comments are sprinkled throughout the notify functions above.
	 */
	wl_signal_add(&cursor->events.motion, &cursor_motion);
	wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
	wl_signal_add(&cursor->events.button, &cursor_button);
	wl_signal_add(&cursor->events.axis, &cursor_axis);
	wl_signal_add(&cursor->events.frame, &cursor_frame);

	cursor_shape_mgr = wlr_cursor_shape_manager_v1_create(dpy, 1);
	wl_signal_add(&cursor_shape_mgr->events.request_set_shape, &request_set_cursor_shape);

	/*
	 * Configures a seat, which is a single "seat" at which a user sits and
	 * operates the computer. This conceptually includes up to one keyboard,
	 * pointer, touch, and drawing tablet device. We also rig up a listener to
	 * let us know when new input devices are available on the backend.
	 */
	wl_signal_add(&backend->events.new_input, &new_input_device);
	virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(dpy);
	wl_signal_add(&virtual_keyboard_mgr->events.new_virtual_keyboard,
			&new_virtual_keyboard);
	virtual_pointer_mgr = wlr_virtual_pointer_manager_v1_create(dpy);
    wl_signal_add(&virtual_pointer_mgr->events.new_virtual_pointer,
            &new_virtual_pointer);

	seat = wlr_seat_create(dpy, "seat0");
	wl_signal_add(&seat->events.request_set_cursor, &request_cursor);
	wl_signal_add(&seat->events.request_set_selection, &request_set_sel);
	wl_signal_add(&seat->events.request_set_primary_selection, &request_set_psel);
	wl_signal_add(&seat->events.request_start_drag, &request_start_drag);
	wl_signal_add(&seat->events.start_drag, &start_drag);

	kb_group = createkeyboardgroup();
	wl_list_init(&kb_group->destroy.link);

	output_mgr = wlr_output_manager_v1_create(dpy);
	wl_signal_add(&output_mgr->events.apply, &output_mgr_apply);
	wl_signal_add(&output_mgr->events.test, &output_mgr_test);

	/* kalin-wm is Wayland-only; make sure nothing inherits a stale X server. */
	unsetenv("DISPLAY");
}

void
spawn(const Arg *arg)
{
	pid_t pid;
	const char *cmd = ((char **)arg->v)[0];
	int errpipe[2];
	
	if (pipe(errpipe) < 0) {
		wlr_log(WLR_ERROR, "Failed to create spawn pipe: %s", strerror(errno));
		return;
	}
	if (fcntl(errpipe[1], F_SETFD, FD_CLOEXEC) < 0) {
		wlr_log(WLR_ERROR, "Failed to set CLOEXEC on spawn pipe: %s", strerror(errno));
		close(errpipe[0]);
		close(errpipe[1]);
		return;
	}

	pid = fork();
	if (pid < 0) {
		wlr_log(WLR_ERROR, "Failed to fork for spawn: %s", strerror(errno));
		close(errpipe[0]);
		close(errpipe[1]);
		return;
	}
	
	if (pid == 0) {
		int err;
		close(errpipe[0]);
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();
		execvp(cmd, (char **)arg->v);
		err = errno;
		/* Best-effort: report exec failure to the parent. Nothing we can
		 * do in the doomed child if the write itself fails. */
		if (write(errpipe[1], &err, sizeof(err)) < 0) { /* ignore */ }
		wlr_log(WLR_ERROR, "Failed to execvp %s: %s", cmd, strerror(err));
		_exit(1);
	}

	close(errpipe[1]);
	{
		struct pollfd pfd = {.fd = errpipe[0], .events = POLLIN};
		int pr = poll(&pfd, 1, 100);
		if (pr > 0 && (pfd.revents & POLLIN)) {
			int err = 0;
			ssize_t n = read(errpipe[0], &err, sizeof(err));
			if (n == (ssize_t)sizeof(err))
				wlr_log(WLR_ERROR, "Spawn failed for %s: %s", cmd, strerror(err));
		}
	}
	close(errpipe[0]);
	
	wlr_log(WLR_DEBUG, "Spawned process %s (pid %d)", cmd, pid);
}

void
startdrag(struct wl_listener *listener, void *data)
{
	struct wlr_drag *drag = data;
	if (!drag->icon)
		return;

	drag->icon->data = &wlr_scene_drag_icon_create(drag_icon, drag->icon)->node;
	LISTEN_STATIC(&drag->icon->events.destroy, destroydragicon);
}

void
tagmon(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		setmon(sel, dirtomon(arg->i));
}

void
togglefloating(const Arg *arg)
{
	Client *sel = focustop(selmon);
	/* return if fullscreen */
	if (sel && !sel->isfullscreen)
		setfloating(sel, !sel->isfloating);
}

void
togglefullscreen(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		setfullscreen(sel, !sel->isfullscreen);
}

void
unlocksession(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, unlock);
	destroylock(lock, 1);
}

void
unmaplayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, unmap);

	l->mapped = 0;
	wlr_scene_node_set_enabled(&l->scene->node, 0);
	if (l == exclusive_focus)
		exclusive_focus = NULL;
	if (l->layer_surface->output && (l->mon = l->layer_surface->output->data))
		arrangelayers(l->mon);
	if (l->layer_surface->surface == seat->keyboard_state.focused_surface)
		focus_top(selmon, 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
unmapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is unmapped, and should no longer be shown. */
	Client *c = wl_container_of(listener, c, unmap);
	int save_w;
	int save_h;
	if (c == grabc) {
		cursor_mode = CurNormal;
		grabc = NULL;
	}

	ftl_destroy(c);

	if (client_is_unmanaged(c)) {
		if (c == exclusive_focus) {
			exclusive_focus = NULL;
			if (selmon)
				focus_top(selmon, 1);
		}
	} else {
		save_w = c->geom.width;
		save_h = c->geom.height;
		if (c->crop.active && c->crop.saved_base && c->crop.base_w > 0 && c->crop.base_h > 0) {
			save_w = c->crop.base_w;
			save_h = c->crop.base_h;
		}
		window_size_history_store(c, save_w, save_h);

		if (c->link.prev && c->link.next)
			wl_list_remove(&c->link);
		setmon(c, NULL);
		if (c->flink.prev && c->flink.next)
			wl_list_remove(&c->flink);
	}

	wlr_scene_node_destroy(&c->scene->node);
	printstatus();
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
updatemons(struct wl_listener *listener, void *data)
{
	/*
	 * Called whenever the output layout changes: adding or removing a
	 * monitor, changing an output's mode or position, etc. This is where
	 * the change officially happens and we update geometry, window
	 * positions, focus, and the stored configuration in wlroots'
	 * output-manager implementation.
	 */
	struct wlr_output_configuration_v1 *config
			= wlr_output_configuration_v1_create();
	Client *c;
	struct wlr_output_configuration_head_v1 *config_head;
	Monitor *m;

	/* First remove from the layout the disabled monitors */
	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output->enabled || m->asleep)
			continue;
		config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);
		config_head->state.enabled = 0;
		/* Remove this output from the layout to avoid cursor enter inside it */
		wlr_output_layout_remove(output_layout, m->wlr_output);
		closemon(m);
		m->m = m->w = (struct wlr_box){0};
	}
	/* Insert outputs that need to */
	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output->enabled
				&& !wlr_output_layout_get(output_layout, m->wlr_output))
			wlr_output_layout_add_auto(output_layout, m->wlr_output);
	}

	/* Now that we update the output layout we can get its box */
	wlr_output_layout_get_box(output_layout, NULL, &sgeom);

	wlr_scene_node_set_position(&root_bg->node, sgeom.x, sgeom.y);
	wlr_scene_rect_set_size(root_bg, sgeom.width, sgeom.height);

	wallpaper_configure(sgeom.width, sgeom.height);
	overlay_clock_configure(sgeom.width, sgeom.height);
	offscreen_indicators_configure(sgeom.width, sgeom.height);

	/* Make sure the clients are hidden when dwl is locked */
	wlr_scene_node_set_position(&locked_bg->node, sgeom.x, sgeom.y);
	wlr_scene_rect_set_size(locked_bg, sgeom.width, sgeom.height);

	wl_list_for_each(m, &mons, link) {
		if (!m->wlr_output->enabled)
			continue;
		config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);

		/* Get the effective monitor geometry to use for surfaces */
		wlr_output_layout_get_box(output_layout, m->wlr_output, &m->m);
		m->w = m->m;
		wlr_scene_output_set_position(m->scene_output, m->m.x, m->m.y);

		wlr_scene_node_set_position(&m->fullscreen_bg->node, m->m.x, m->m.y);
		wlr_scene_rect_set_size(m->fullscreen_bg, m->m.width, m->m.height);

		if (m->lock_surface) {
			struct wlr_scene_tree *scene_tree = m->lock_surface->surface->data;
			wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
			wlr_session_lock_surface_v1_configure(m->lock_surface, m->m.width, m->m.height);
		}

		/* Calculate the effective monitor geometry to use for clients */
		arrangelayers(m);
		/* Don't move clients to the left output when plugging monitors */
		arrange(m);
		/* make sure fullscreen clients have the right size */
		if ((c = focustop(m)) && c->isfullscreen)
			resize(c, m->m, 0);

		/* Try to re-set the gamma LUT when updating monitors,
		 * it's only really needed when enabling a disabled output, but meh. */
		m->gamma_lut_changed = 1;

		config_head->state.x = m->m.x;
		config_head->state.y = m->m.y;

		if (!selmon) {
			selmon = m;
		}
	}

	if (selmon && selmon->wlr_output->enabled) {
		wl_list_for_each(c, &clients, link) {
			if (!c->mon && client_surface(c)->mapped)
				setmon(c, selmon);
		}
		focus_top(selmon, 1);
		if (selmon->lock_surface) {
			client_notify_enter(selmon->lock_surface->surface,
					wlr_seat_get_keyboard(seat));
			client_activate_surface(selmon->lock_surface->surface, 1);
		}
	}

	/* FIXME: figure out why the cursor image is at 0,0 after turning all
	 * the monitors on.
	 * Move the cursor image where it used to be. It does not generate a
	 * wl_pointer.motion event for the clients, it's only the image what it's
	 * at the wrong position after all. */
	wlr_cursor_move(cursor, NULL, 0, 0);

	wlr_output_manager_v1_set_configuration(output_mgr, config);
}

void
updatetitle(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_title);
	ftl_update_title(c);
	if (c == focustop(c->mon))
		printstatus();
}

void
urgent(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	Client *c = NULL;
	toplevel_from_wlr_surface(event->surface, &c, NULL);
	if (!c || c == focustop(selmon))
		return;

	c->isurgent = 1;
	printstatus();

	if (client_surface(c)->mapped)
		client_set_border_color(c, urgentcolor);
}

void
virtualkeyboard(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_keyboard_v1 *kb = data;
	/* virtual keyboards shouldn't share keyboard group */
	KeyboardGroup *group = createkeyboardgroup();
	/* Set the keymap to match the group keymap */
	wlr_keyboard_set_keymap(&kb->keyboard, group->wlr_group->keyboard.keymap);
	LISTEN(&kb->keyboard.base.events.destroy, &group->destroy, destroykeyboardgroup);

	/* Add the new keyboard to the group */
	wlr_keyboard_group_add_keyboard(group->wlr_group, &kb->keyboard);
}

void
virtualpointer(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	struct wlr_input_device *device = &event->new_pointer->pointer.base;

	wlr_cursor_attach_input_device(cursor, device);
	if (event->suggested_output)
		wlr_cursor_map_input_to_output(cursor, device, event->suggested_output);
}

Monitor *
xytomon(double x, double y)
{
	struct wlr_output *o = wlr_output_layout_output_at(output_layout, x, y);
	Monitor *m;

	if (o)
		return o->data;
	if (selmon)
		return selmon;
	if (!wl_list_empty(&mons)) {
		m = wl_container_of(mons.next, m, link);
		return m;
	}
	return NULL;
}

void
xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny)
{
	struct wlr_scene_node *node, *pnode;
	struct wlr_surface *surface = NULL;
	Client *c = NULL;
	LayerSurface *l = NULL;
	int layer;

	for (layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
		if (!(node = wlr_scene_node_at(&layers[layer]->node, x, y, nx, ny)))
			continue;

		if (node->type == WLR_SCENE_NODE_BUFFER)
			surface = wlr_scene_surface_try_from_buffer(
					wlr_scene_buffer_from_node(node))->surface;
		/* Walk the tree to find a node that knows the client */
		for (pnode = node; pnode && !c; pnode = &pnode->parent->node)
			c = pnode->data;
		if (c && c->type == LayerShell) {
			c = NULL;
			l = pnode->data;
		}
	}

	if (psurface) *psurface = surface;
	if (pc) *pc = c;
	if (pl) *pl = l;
}

void
zoom(const Arg *arg)
{
	Client *c, *sel = focustop(selmon);

	if (!sel || !selmon || !selmon->lt[selmon->sellt]->arrange || sel->isfloating)
		return;

	/* Search for the first tiled window that is not sel, marking sel as
	 * NULL if we pass it along the way */
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, selmon) && !c->isfloating) {
			if (c != sel)
				break;
			sel = NULL;
		}
	}

	/* Return if no other tiled window was found */
	if (&c->link == &clients)
		return;

	/* If we passed sel, move c to the front; otherwise, move sel to the
	 * front */
	if (!sel)
		sel = c;
	wl_list_remove(&sel->link);
	wl_list_insert(&clients, &sel->link);

	focusclient(sel, 1);
	arrange(selmon);
}


int
main(int argc, char *argv[])
{
	char *startup_cmd = NULL;
	int c;

	while ((c = getopt(argc, argv, "s:hdv")) != -1) {
		if (c == 's')
			startup_cmd = optarg;
		else if (c == 'd')
			log_level = WLR_DEBUG;
		else if (c == 'v')
			die("dwl " VERSION);
		else
			goto usage;
	}
	if (optind < argc)
		goto usage;

	/* Wayland requires XDG_RUNTIME_DIR for creating its communications socket */
	if (!getenv("XDG_RUNTIME_DIR"))
		die("XDG_RUNTIME_DIR must be set");
	setup();
	run(startup_cmd);
	cleanup();
	return EXIT_SUCCESS;

usage:
	die("Usage: %s [-v] [-d] [-s startup command]", argv[0]);
}
