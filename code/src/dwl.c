/*
 * See LICENSE file for copyright and license details.
 */
#include <errno.h>
#include <getopt.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <math.h>
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

#include <fcntl.h>
#include <sys/types.h>

/* Shared data model + macros (MAX/MIN/LENGTH/LISTEN/VISIBLEON/...) live in
 * kalin.h. dwl.c OWNS the runtime globals as file-scope statics, so it defines
 * DWL_INTERNAL to pull in only the types and skip kalin.h's extern block. */
#define DWL_INTERNAL
#include "kalin.h"
#include "binds.h"
#include "shaders.h"
#include "modules/shaders/window_shader_math.h"

/* Crop editor state. Type lives in kalin.h; the crop/ipc TUs link against this
 * instance, so it has external linkage. */
CropEditor crop_editor;

/* Screenshot UI state. Type lives in kalin.h; the screenshot_ui TU links
 * against this instance, so it has external linkage. */
ScreenshotEditor screenshot_ui;

/* Whichever docked client (see setdocked()) the cursor is currently over, or
 * NULL. Updated in motionnotify(); read by ipc.c to mirror into the state
 * broadcast so a shell panel can auto-hide a docked terminal when the cursor
 * leaves it — the compositor is the only thing that can know this, since a
 * docked client is a real toplevel and its own hover state isn't visible to
 * the shell any other way. External linkage so ipc.c can read it. */
Client *dock_hover_client;

/* dockprep table lives in modules/dock/dockprep.c (#included below); these
 * forward-decls satisfy mapnotify()'s earlier dockprep_consume() call. */
void dockprep_register(const char *appid, struct wlr_box rect);
int dockprep_consume(const char *appid, struct wlr_box *out);

/* Camera defaults for a fresh monitor (multi-camera: every Monitor owns its
 * own `cam` Viewport over the shared world — see obsidian/multi-camera.md;
 * initialized in createmon()). */
static const Viewport cam_defaults = { 0, 0, 0, 0, 1.0, 1.0, 1, 1, 1, 0, 0, 0, 0 };

/* True scene-zoom transform: screen = (world - mon camera) * zoom + mon
 * layout offset. Shared via kalin.h (MON_ZOOM_SAFE, WORLD_TO_SCREEN_X/Y,
 * SCREEN_TO_WORLD_X/Y) since modules/layout/connection_graph.c needs them
 * too. Window SIZES are scaled by zoom in resize(); positions by these
 * macros. */

/* Exit confirmation state */
static struct {
	time_t last_press;       /* time of first exit keypress */
	int pending;             /* 1 = waiting for confirmation */
} exit_confirm = { 0, 0 };

/* State bits mirrored to the IPC shell (quickshell hold-Super window-actions
 * overlay + "press Esc again to quit" prompt). Owned here as external-linkage
 * globals; the ipc TU reads them via kalin.h. */
int super_held = 0;
int exit_pending = 0;
int menu_shown = 0;   /* hold-Super window menu visible (broadcast to the shell) */
/* Stable per-client id, assigned once at map time — used to reference clients
 * in the IPC connection-graph broadcast/sever command (appid+title isn't
 * unique across multiple instances of the same app). */
static uint32_t next_client_id = 1;
/* SPAWN_GAP (gap between a new window and its spawn-parent) is now in
 * kalin.h — modules/layout/connection_graph.c needs it too. */
/* Clears exit_pending/exit_confirm after the confirmation window so each fresh
 * arming re-broadcasts a 0->1 edge the shell can flash on. */
static struct wl_event_source *exit_confirm_timer;

/* Stationary wallpaper background. Type lives in kalin.h; the wallpaper TU links
 * against this instance (external linkage). */
Wallpaper wallpaper;

/* Window-size-history lookup (defined in the separately-compiled
 * modules/layout/window_size_history.c TU). */


/* function declarations */
static void applybounds(Client *c, struct wlr_box *bbox);
static void applyrules(Client *c);
void arrange(Monitor *m);
void viewport_camera_tick(Monitor *m);
void arrange_mark_dirty(Monitor *m);
void status_mark_dirty(void);
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
static void createmon(struct wl_listener *listener, void *data);
static void createnotify(struct wl_listener *listener, void *data);
static void createpointer(struct wlr_pointer *pointer);
static void createpointerconstraint(struct wl_listener *listener, void *data);
void gestures_attach(struct wlr_pointer *pointer);
static void createpopup(struct wl_listener *listener, void *data);
static void cursorconstrain(struct wlr_pointer_constraint_v1 *constraint);
static void cursorframe(struct wl_listener *listener, void *data);
static void cursorwarptohint(void);
static void destroydecoration(struct wl_listener *listener, void *data);
static void destroydragicon(struct wl_listener *listener, void *data);
static void destroyidleinhibitor(struct wl_listener *listener, void *data);
static void destroylayersurfacenotify(struct wl_listener *listener, void *data);
static void destroynotify(struct wl_listener *listener, void *data);
static void destroypointerconstraint(struct wl_listener *listener, void *data);
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
static void killclient(const Arg *arg);
static void mapnotify(struct wl_listener *listener, void *data);
static void maximizenotify(struct wl_listener *listener, void *data);
int window_size_history_load(Client *c, int *width, int *height);
void window_size_history_store(Client *c, int width, int height);

static void motionabsolute(struct wl_listener *listener, void *data);
/* Not static: session_lock.c's destroylock() calls this (with all-zero args)
 * to refresh pointer focus right after a session unlock. */
void motionnotify(uint32_t time, struct wlr_input_device *device, double sx,
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
void printstatus(void);
int clients_anim_step(void);
static void powermgrsetmode(struct wl_listener *listener, void *data);
static void quit(const Arg *arg);
/* Defined in the separately-compiled viewport_ops TU. */
void viewport_pan(const Arg *arg);
void viewport_zoom(const Arg *arg);
void viewport_reset(const Arg *arg);
void viewport_fit_all(const Arg *arg);
void viewport_center_on(Client *c);
void viewport_menu_reveal(Client *c);
void viewport_toggle_follow(const Arg *arg);
void viewport_toggle_follow_new(const Arg *arg);
void viewport_follow_focus(void);
void viewport_pan_grab_start(void);
void viewport_pan_grab_update(void);
void viewport_tick(void);
/* Defined in the separately-compiled overview TU. */
void toggle_overview(const Arg *arg);
void overview_exit(void);
void overview_select(Client *c);
int overview_is_active(void);
/* Defined in the separately-compiled toplevel_export TU. */
void toplevel_export_init(struct wl_display *display);
/* Defined in the separately-compiled toplevel_icon TU. */
void toplevel_icon_init(struct wl_display *display);
/* Defined in the separately-compiled system_bell TU. */
void system_bell_init(struct wl_display *display);
/* Defined in the separately-compiled wallpaper TU. */
void wallpaper_configure(int w, int h);
void wallpaper_update(void);
/* Defined in the separately-compiled capture TU. */
void capture_screenshot(const Arg *arg);

/* Defined in the separately-compiled crop_mode TU. */
void cropbegin(const Arg *arg);
void cropcancel(const Arg *arg);
void cropreset(const Arg *arg);
void cropend(const Arg *arg);
void cropdraw(void);

/* Defined in the separately-compiled screenshot_ui TU. */
void screenshotui_begin(const Arg *arg);
void screenshotui_cancel(const Arg *arg);
void screenshotui_confirm(bool write_to_disk);
void screenshotui_toggle_pointer(void);
void screenshotui_draw(void);
void screenshotui_hover(void);

/* Defined in the separately-compiled session_lock TU. */
void destroylocksurface(struct wl_listener *listener, void *data);
void session_lock_init(void);
void session_lock_resize(void);
void session_lock_configure_output(Monitor *m);
void session_lock_cleanup(void);
static void rendermon(struct wl_listener *listener, void *data);
static void requestdecorationmode(struct wl_listener *listener, void *data);
static void requeststartdrag(struct wl_listener *listener, void *data);
static void requestmonstate(struct wl_listener *listener, void *data);
static void client_apply_crop_clip(Client *c);
static void client_apply_zoom_frame(Client *c);
void resize(Client *c, struct wlr_box geo, int interact);
void client_set_target_geom(Client *c, struct wlr_box geo);
static void run(char *startup_cmd);
static void setcursor(struct wl_listener *listener, void *data);
static void setcursorshape(struct wl_listener *listener, void *data);
void setfullscreen(Client *c, int fullscreen);
void setmaximized(Client *c, int maximized);
static void setontop(Client *c, int ontop);
void setminimized(Client *c, int minimized);
void setdocked(Client *c, int docked, struct wlr_box rect);
Client *client_find_by_appid(const char *appid);
void resizefocused(const Arg *arg);
void fitwidth(const Arg *arg);
void fitheight(const Arg *arg);
static void setmon(Client *c, Monitor *m);
static void setpsel(struct wl_listener *listener, void *data);
static void setsel(struct wl_listener *listener, void *data);
static void setup(void);
/* spawn() is public (also declared in kalin.h for config keybinds); spawn_named()
 * and tmux_kill_window() are internal. All three are defined in the #include'd
 * modules/spawn/tmux_spawn.c below; forward-declared here because dwl.c is its
 * own translation unit (it does not include kalin.h). */
void spawn(const Arg *arg);
static void spawn_named(const Arg *arg, const char *window_name);
static int tmux_kill_window(const char *window_name);
static void startdrag(struct wl_listener *listener, void *data);
static void tagmon(const Arg *arg);
static void togglefullscreen(const Arg *arg);
void togglemaximized(const Arg *arg);
void toggleontop(const Arg *arg);
void toggleoverlap(const Arg *arg);
static void togglepaper(const Arg *arg);
static void paperyellow(const Arg *arg);
static void client_apply_paper(Client *c);
void toggleminimize(const Arg *arg);
void togglescratchpad(const Arg *arg);
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

/* variables */
static pid_t child_pid = -1;
int locked;  /* extern; consumed by modules/input/keyboard.c */
static void *exclusive_focus;
struct wl_display *dpy;  /* extern; consumed by modules/session_lock.c */
struct wl_event_loop *event_loop;
static struct wlr_backend *backend;
struct wlr_scene *scene;
struct wlr_scene_tree *layers[NUM_LAYERS];
static struct wlr_scene_tree *drag_icon;
/* Map from ZWLR_LAYER_SHELL_* constants to Lyr* enum */
static const int layermap[] = { LyrBg, LyrBottom, LyrTop, LyrOverlay };
struct wlr_renderer *drw;
struct wlr_allocator *alloc;
static struct wlr_compositor *compositor;
static struct wlr_session *session;

static struct wlr_xdg_shell *xdg_shell;
static struct wlr_xdg_activation_v1 *activation;
struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_mgr;
static struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
struct wl_list clients; /* tiling order */
struct wl_list fstack;  /* focus order (extern; consumed by layout_world.c) */
struct wl_list static_listeners;
struct wlr_idle_notifier_v1 *idle_notifier;  /* extern; consumed by modules/input/keyboard.c */
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

struct wlr_cursor *cursor;
struct wlr_xcursor_manager *cursor_mgr;

static struct wlr_scene_rect *root_bg;

struct wlr_seat *seat;  /* extern; consumed by modules/input/keyboard.c */
static KeyboardGroup *kb_group;
static unsigned int cursor_mode;
static Client *grabc;
static int grabcx, grabcy; /* client-relative */
static int resize_anchor_x, resize_anchor_y; /* world-space, fixed corner opposite the grab */

struct wlr_output_layout *output_layout;
struct wlr_box sgeom;  /* extern; consumed by modules/session_lock.c */
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

/* Directional focus navigation (defined in the separately-compiled
 * modules/layout/directional_focus.c TU) - forward declaration for
 * config.h. */
void focus_directional(const Arg *arg);
/* Connection-graph (defined in the separately-compiled
 * modules/layout/connection_graph.c TU) - forward declaration for config.h,
 * same as focus_directional above. */
void swap_neighbor_dir(const Arg *arg);

/* configuration, allows nested code to access above variables */
#include "config.h"

/* attempt to encapsulate suck into one file */
#include "client_inline.h"

/* Connection-graph (defined in the separately-compiled
 * modules/layout/connection_graph.c TU). kalin.h also declares
 * connect_clients()/resolve_growth_overlap()/sever_connection() for other
 * modules (resize_actions.c, ipc.c) — but dwl.c itself doesn't see that
 * section of kalin.h (it's guarded by "#ifndef DWL_INTERNAL", which dwl.c
 * defines, since dwl.c owns these symbols' *other* declarations there
 * directly), so every one of these functions dwl.c calls needs its own
 * forward declaration here too, independent of kalin.h's copy. */
void connect_clients(Client *a, Client *b);
void resolve_growth_overlap(Client *c);
void sever_connection(uint32_t id_a, uint32_t id_b);
void sever_cross_monitor_edges(Client *c);
int opposite_octant(int oct);
int connection_click_hit(double sx, double sy, uint32_t *out_a, uint32_t *out_b);
void close_gap(Client *a, Client *b);
int collect_component(Client *start, Client **out, int max);
void connect_pick_arm(void);
void connect_pick_cancel(void);
void connect_pick_complete(Client *target);
Client *connect_pick_pending(void);

/* wlr-foreign-toplevel-management (defined in modules/foreign_toplevel.c). */
void ftl_create(Client *c);
void ftl_destroy(Client *c);
void ftl_update_title(Client *c);
void ftl_sync_state(void);

/* Shell IPC socket (defined in the separately-compiled modules/ipc.c TU). */
void ipc_init(const char *wl_display_name);
void ipc_broadcast_state(void);
void ipc_finish(void);

/* Keyboard event dispatch (defined in the separately-compiled
 * modules/input/keyboard.c TU); keybinding() itself stays here (below) since it
 * depends on the static compiled-fallback action functions. */
void keypress(struct wl_listener *listener, void *data);
void keypressmod(struct wl_listener *listener, void *data);
int keyrepeat(void *data);

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
			if (r->paper)
				c->paper_mode = 1;
			i = 0;
			wl_list_for_each(m, &mons, link) {
				if (r->monitor == i++)
					mon = m;
			}
		}
	}

	setmon(c, mon);
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

	/* Every window is free-positioned now (no layout modes to switch between),
	 * so the only remaining z-order concern is fullscreen/pin-on-top, which
	 * setfullscreen()/setontop() already keep correct on their own transitions
	 * — no per-arrange reparenting sweep needed here any more. */
	motionnotify(0, NULL, 0, 0, 0, 0);
	checkidleinhibitor(NULL);
}

/* Position and size a client's frame (scene-node origin, border rects, focus
 * ring) for the current camera zoom/pan — everything resize() derives from
 * c->geom that ISN'T the client's actual configured buffer size (that part,
 * cfg_w/cfg_h, is zoom-independent — the client always renders at its native
 * logical size; only the *displayed* frame and border thickness scale, via
 * this and via client_set_buffer_scale()'s dest_size).
 *
 * Shared by resize() (on a real geometry change) and viewport_camera_tick()
 * (every frame during a camera-only pan/zoom). Without the latter, a smooth
 * camera zoom left the border/focus-ring rects at whatever size the last real
 * resize() computed — client_set_buffer_scale() already rescales the buffer
 * every frame in rendermon(), so the content tracked the zoom but the frame
 * around it didn't, visibly lagging/mismatched until the next resize() (e.g.
 * on animation settle) caught it up. Most noticeable on a big, sudden zoom
 * like the overview's fit-all jump. */
static void
client_apply_zoom_frame(Client *c)
{
	int view_x, view_y;
	float zf;
	int z_bw, z_w, z_h, z_inner_h;

	if (!c->scene || !c->border[0] || !c->border[1] || !c->border[2] || !c->border[3])
		return;

	/* Fullscreen always fills the physical monitor 1:1, regardless of camera
	 * pan/zoom — c->geom is already the monitor's own output-layout rect
	 * (set from c->mon->m in setfullscreen()), and that rect must land
	 * exactly on the monitor's own region of the shared layout space for
	 * wlr_scene_output to actually show it full-screen; running it through
	 * the WORLD_TO_SCREEN camera transform would shift/shrink it away from
	 * the monitor's real position whenever the camera isn't at pan=(0,0)
	 * zoom=1, and out of position often enough to render partially off the
	 * monitor's output region entirely.
	 *
	 * Maximized (setmaximized()) needs the exact same bypass: c->geom there
	 * is c->mon->w (also output-layout/screen space, not world space), for
	 * the same reason — it's meant to visually fill the monitor's work area
	 * as-is, not "fill the world-space region currently under the camera".
	 *
	 * Docked (setdocked()) needs it too: c->geom there is a screen-space rect
	 * a shell panel handed the compositor over IPC, and it must stay glued to
	 * that panel's on-screen position regardless of the canvas being panned
	 * or zoomed underneath it. */
	if (client_is_camera_bypassed(c)) {
		view_x = c->geom.x;
		view_y = c->geom.y;
		zf = 1.0f;
	} else {
		view_x = WORLD_TO_SCREEN_X(c->mon, c->geom.x);
		view_y = WORLD_TO_SCREEN_Y(c->mon, c->geom.y);
		zf = MON_ZOOM_SAFE(c->mon);
	}
	z_bw = (int)lroundf(c->bw * zf);
	z_w  = MAX(1, (int)lroundf(c->geom.width * zf));
	z_h  = MAX(1, (int)lroundf(c->geom.height * zf));
	z_inner_h = MAX(0, z_h - 2 * z_bw);

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
}

/* Camera-only refresh: the parts of arrange() that actually depend on
 * viewport.x/y/zoom, and nothing else. A pure camera pan or zoom never
 * changes any window's own geometry, so re-running all of arrange() on every
 * ~60Hz animation tick while panning/zooming would be pure waste.
 *
 * But the frame's on-screen *position* is WORLD_TO_SCREEN(c->geom), which
 * depends on viewport.x/y/zoom directly and must be re-applied every tick
 * regardless — skipping that (an earlier version of this function did) made
 * windows sit frozen at their old screen position for the whole animation,
 * only snapping into place at settle when the full arrange() next ran,
 * because nothing else reliably re-applies it mid-animation (a client's own
 * surface commits happening to trigger resize() is incidental, not
 * guaranteed, and made most idle windows look stuck/janky during any pan).
 * viewport_tick() calls this per tick instead of arrange(); at settle,
 * viewport_step_cam() additionally re-syncs each client (a per-client resize())
 * to catch the borders/clip/buffer-scale a camera-only refresh doesn't cover —
 * NOT the full arrange() an older comment here claimed. */
void
viewport_camera_tick(Monitor *m)
{
	Client *c;

	if (!m || !m->wlr_output->enabled)
		return;
	wallpaper_update();

	wl_list_for_each(c, &clients, link) {
		/* Clients already gliding via clients_anim_step() (rendermon(), a
		 * column-layout move in progress) reposition themselves every frame
		 * from their own spring state — skip them here to avoid stepping on
		 * that with a stale c->geom (this runs before clients_anim_step() in
		 * rendermon()'s per-frame order). */
		if (c->mon != m || c->animating || !c->scene)
			continue;
		client_apply_zoom_frame(c);
	}

	/* World content slides under a stationary screen-space cursor while
	 * panning, so which window/surface is "under" the pointer can change even
	 * though the pointer itself hasn't moved — needs a re-check every tick. */
	motionnotify(0, NULL, 0, 0, 0, 0);
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
		arrange_mark_dirty(m);
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
	double delta, factor;
	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
	/* Scroll bindings: a modifier + scroll can fire an action. A wheel
	 * dispatches once per discrete tick; touchpad finger scroll is a stream
	 * of fine deltas instead, so those accumulate and dispatch once per
	 * scroll_bind_step (config.h) of travel. While a matching bind exists
	 * every finger event is swallowed — sub-step deltas must not leak to
	 * the client mid-gesture — and the tap/hold gesture is interrupted so
	 * holding the modifier through a long pan can't pop its hold bind. */
	if (binds_active() && event->delta != 0) {
		struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat);
		uint32_t mods = kb ? wlr_keyboard_get_modifiers(kb) : 0;
		if (CLEANMASK(mods)) {
			uint32_t dir;
			if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL)
				dir = event->delta < 0 ? SCROLL_UP : SCROLL_DOWN;
			else
				dir = event->delta < 0 ? SCROLL_LEFT : SCROLL_RIGHT;
			if (event->source == WL_POINTER_AXIS_SOURCE_WHEEL
					|| event->source == WL_POINTER_AXIS_SOURCE_WHEEL_TILT) {
				if (bind_dispatch_scroll(mods, dir))
					return;
			} else if (bind_scroll_bound(mods, dir)) {
				static double acc[2];
				double step = scroll_bind_step >= 1.0 ? scroll_bind_step : 1.0;
				int vert = event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL;
				bind_gesture_interrupt();
				if (acc[vert] != 0.0 && (acc[vert] > 0.0) != (event->delta > 0.0))
					acc[vert] = 0.0; /* direction flip: drop stale partial travel */
				acc[vert] += event->delta;
				while (fabs(acc[vert]) >= step) {
					acc[vert] -= copysign(step, acc[vert]);
					bind_dispatch_scroll(mods, dir);
				}
				return;
			}
		}
	}
	/* Per-source speed factor + optional finger-scroll smoothing (config.h).
	 * A zero finger delta is the scroll-stop event: forward it untouched
	 * (clients key kinetic scrolling off it) and reset the filter so the
	 * next flick doesn't inherit stale momentum. */
	factor = (event->source == WL_POINTER_AXIS_SOURCE_WHEEL
			|| event->source == WL_POINTER_AXIS_SOURCE_WHEEL_TILT)
			? scroll_factor_wheel : scroll_factor_finger;
	delta = event->delta * factor;
	if (event->source == WL_POINTER_AXIS_SOURCE_FINGER) {
		static double smooth[2];
		int vert = event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL;
		if (event->delta == 0)
			smooth[vert] = 0;
		else
			delta = smooth[vert] = scroll_smoothing * smooth[vert]
					+ (1.0 - scroll_smoothing) * delta;
	}
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(seat,
			event->time_msec, event->orientation, delta,
			(int32_t)lround(event->delta_discrete * factor),
			event->source, event->relative_direction);
}

void
buttonpress(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_button_event *event = data;
	struct wlr_keyboard *keyboard;
	uint32_t mods;
	Client *c;

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	switch (event->state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		/* A button press during a modifier hold cancels any arming tap/hold, so
		 * releasing Super after e.g. Super+MiddleClick doesn't fire the launcher. */
		bind_gesture_interrupt();
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

		/* Screenshot UI: start drawing a custom selection on left click
		 * (overrides the whole-monitor default it opened with). */
		if (screenshot_ui.active && event->button == BTN_LEFT) {
			screenshot_ui.dragging = true;
			screenshot_ui.start_x = cursor->x;
			screenshot_ui.start_y = cursor->y;
			screenshot_ui.end_x = cursor->x;
			screenshot_ui.end_y = cursor->y;
			screenshotui_draw();
			return;
		}

		/* Click-to-sever a spawn-connection line: only while Super is held
		 * (matching when quickshell actually draws the lines) and only on
		 * BTN_LEFT, and only if the click didn't land on a client (a real
		 * window always takes priority over a line running behind/near it).
		 * Entering CurCut here (rather than just testing this one point) lets
		 * motionnotify() keep re-testing every subsequent cursor position for
		 * the rest of the drag, so severing a line no longer needs a precise
		 * single click — sweeping the cursor near/across it while the button
		 * is held cuts it too (a plain click is just the zero-motion case of
		 * the same sweep). */
		if (super_held && event->button == BTN_LEFT) {
			Client *hit, *pending = connect_pick_pending();
			xytonode(cursor->x, cursor->y, NULL, &hit, NULL, NULL, NULL);
			if (!hit) {
				uint32_t id_a, id_b;
				if (pending)
					connect_pick_cancel();
				if (connection_click_hit(cursor->x, cursor->y, &id_a, &id_b))
					sever_connection(id_a, id_b);
				cursor_mode = CurCut;
				return;
			} else if (pending && hit != pending) {
				/* Menu-armed create: the pending source was set by
				 * connect_pick_arm() (Super+L, WindowActions.qml's "Link"
				 * button). The next click on a *different* window completes
				 * it; connect_clients() already no-ops on an occupied slot
				 * or an existing link, so this is safe to always attempt. */
				connect_pick_complete(hit);
				return;
			}
		}

		/* Change focus if the button was _pressed_ over a client */
		xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
		if (c && (!client_is_unmanaged(c) || client_wants_focus(c))) {
			focusclient(c, 1);
			/* Clicking a window while the overview is open both focuses it
			 * (above) and pans/zooms the camera to it at 1.0 zoom — jumping
			 * to what you clicked, rather than restoring the pre-Super+O view
			 * (that's what closing without clicking, overview_exit(), does).
			 * Skipped while Super is held: Super+BTN_LEFT/BTN_RIGHT are about
			 * to start a move/resize grab below (bind_dispatch_button()), and
			 * jumping the camera away here would both cancel that drag before
			 * it starts and defeat rearranging windows while overview is open
			 * — the whole point of being able to see everything at once. */
			if (overview_is_active() && !super_held)
				overview_select(c);
		}

		keyboard = wlr_seat_get_keyboard(seat);
		mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
		if (bind_dispatch_button(mods, event->button)) {
			/* bind_dispatch_button() returning true only means a bind
			 * MATCHED this chord, not that its action actually grabbed
			 * anything — pointer-move/pointer-resize/viewport.pan-grab all
			 * silently no-op if there's nothing under the cursor to act on
			 * (see moveresize()/the ACT_VIEWPORT_PAN_GRAB case), leaving
			 * cursor_mode at CurPressed. Swallowing the click unconditionally
			 * here meant a Super-held click could NEVER reach a client
			 * surface sitting where nothing was grabbable (e.g. a quickshell
			 * overlay's own clickable area) — every Super+click was eaten
			 * before wlr_seat_pointer_notify_button() ever ran. Only swallow
			 * it when a grab genuinely started. */
			if (cursor_mode == CurMove || cursor_mode == CurMoveSolo || cursor_mode == CurResize || cursor_mode == CurPan)
				return;
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

		/* Screenshot UI: releasing just fixes the drawn selection — the mode
		 * stays open until an explicit confirm/cancel key (matching niri). */
		if (screenshot_ui.active && event->button == BTN_LEFT && screenshot_ui.dragging) {
			screenshot_ui.end_x = cursor->x;
			screenshot_ui.end_y = cursor->y;
			screenshot_ui.dragging = false;
			screenshotui_draw();
			return;
		}

		/* If you released any buttons, we exit interactive move/resize/pan mode. */
		if (!locked && cursor_mode != CurNormal && cursor_mode != CurPressed) {
			int was_move = (cursor_mode == CurMove || cursor_mode == CurMoveSolo);
			/* Fallback shape, for when no surface (or a client that never
			 * sets a cursor) ends up under the pointer below. */
			wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
			cursor_mode = CurNormal;
			if (grabc) {
				/* Drop the window off on its new monitor */
				selmon = xytomon(cursor->x, cursor->y);
				setmon(grabc, selmon);
				/* Every window is free-positioned now — a drag just ends
				 * wherever it was dropped, no re-tile/snap. Persist it (and
				 * anything dragged along via the connection graph) right
				 * away so the position survives a restart. */
				if (was_move)
					persistence_save();
			}
			grabc = NULL;
			/* setcursor()/setcursorshape() dropped client requests for the
			 * whole grab, so the surface under the cursor still believes its
			 * preferred shape is set. A bare re-enter is a wlroots no-op
			 * while the focused surface is unchanged, so force a leave
			 * first: the enter from motionnotify() then makes the client
			 * re-request its cursor, instead of the "default" fallback
			 * above staying pinned until the next surface crossing. */
			wlr_seat_pointer_notify_clear_focus(seat);
			motionnotify(0, NULL, 0, 0, 0, 0);
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
	session_lock_cleanup();

	wl_list_for_each_safe(sl, tmp, &static_listeners, link) {
		wl_list_remove(&sl->listener.link);
		wl_list_remove(&sl->link);
		free(sl);
	}
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

	/* Allow a client to apply its own requested content size (e.g. an app
	 * restoring its last window size) while the compositor still controls
	 * placement, bounds, and fullscreen. Except right after a persisted
	 * size restore (see persist_size_pending's comment in kalin.h): this
	 * client's own first post-map commit is finalizing whatever size it
	 * natively chose *before* it's had a chance to see our restored size
	 * requested via resize() below, and would silently clobber the
	 * restore if allowed through — skip exactly that one commit's accept,
	 * then behave normally again. */
	if (c->persist_size_pending)
		c->persist_size_pending = 0;
	else if (client_accept_requested_size(c))
		resolve_growth_overlap(c);

	resize(c, c->geom, 0);

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
	gestures_attach(pointer);
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

	/* Layer-shell surfaces (the quickshell bar, wallpaper, etc.) had no
	 * destroy/unmap logging at all — a bar disappearing for any reason
	 * (client exit, client crash, or a bug on our side) left zero trace in
	 * kalin-wm's own log, only in the client's own stdout if it happened to
	 * be captured somewhere (see the kalinwm dev launcher's /tmp/kalinwm.log
	 * and the note on quickshell-shell). This at least records that the
	 * surface went away and which one, from the compositor's side too. */
	wlr_log(WLR_INFO, "layer-shell surface destroyed: namespace=\"%s\" layer=%d",
			l->layer_surface->namespace ? l->layer_surface->namespace : "(null)",
			l->layer_surface->pending.layer);

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
	status_mark_dirty();

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
	/* While the hold-Super menu is up, focus is locked to whichever window it
	 * opened on: switching focus out from under it would both reposition the
	 * menu (see WindowActions.qml's dockMode/arc anchor) and re-pan the
	 * camera (viewport_follow_focus()) while the user is just trying to read
	 * the menu's key hints, which felt chaotic more than useful. */
	if (menu_shown)
		return;
	if (!sel || (sel->isfullscreen && !client_has_children(sel)))
		return;
	if (arg->i > 0) {
		wl_list_for_each(c, &sel->link, link) {
			if (&c->link == &clients)
				continue; /* wrap past the sentinel node */
			/* Panels (c->ispanel) aren't cycle-focus targets — chrome, not
			 * a window to Super+Tab onto. */
			if (VISIBLEON(c, selmon) && !c->ispanel)
				break; /* found it */
		}
	} else {
		wl_list_for_each_reverse(c, &sel->link, link) {
			if (&c->link == &clients)
				continue; /* wrap past the sentinel node */
			if (VISIBLEON(c, selmon) && !c->ispanel)
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

/*
 * Run a bind-engine action resolved from the DSL. Lives here (not in the
 * engine TU) so the static action functions stay visible. The engine passes
 * semantic ints (directions, etc.); we translate them to the concrete
 * wlroots enums / pointers the functions expect.
 */
void
bind_invoke(int action_id, const Arg *arg)
{
	switch (action_id) {
	case ACT_SPAWN:             spawn(arg); break;
	case ACT_TOGGLE_LAUNCHER:
		/* Toggle the tap-launcher: tracked via tmux itself (window
		 * "launcher" in the "kalin-apps" session, see spawn_named()), not a
		 * pid — try to close it first; if it wasn't open, open it instead. */
		if (!tmux_kill_window("launcher"))
			spawn_named(arg, "launcher");
		break;
	case ACT_CLOSE:             killclient(arg); break;
	case ACT_RESIZE:            resizefocused(arg); break;
	case ACT_VIEWPORT_ZOOM:     viewport_zoom(arg); break;
	case ACT_VIEWPORT_PAN:      viewport_pan(arg); break;
	case ACT_VIEWPORT_FIT:      viewport_fit_all(arg); break;
	case ACT_VIEWPORT_RESET:    viewport_reset(arg); break;
	case ACT_VIEWPORT_FOLLOW:   viewport_toggle_follow(arg); break;
	case ACT_VIEWPORT_FOLLOW_NEW: viewport_toggle_follow_new(arg); break;
	case ACT_FOCUS_DIR:         focus_directional(arg); break;
	case ACT_SWAP_DIR:          swap_neighbor_dir(arg); break;
	case ACT_FOCUS_STACK:       focusstack(arg); break;
	case ACT_TOGGLE_FULLSCREEN: togglefullscreen(arg); break;
	case ACT_TOGGLE_MAXIMIZED: togglemaximized(arg); break;
	case ACT_FIT_WIDTH:  fitwidth(arg); break;
	case ACT_FIT_HEIGHT: fitheight(arg); break;
	case ACT_TOGGLE_ONTOP:     toggleontop(arg); break;
	case ACT_TOGGLE_OVERLAP:   toggleoverlap(arg); break;
	case ACT_LINK_PICK:        connect_pick_arm(); break;
	case ACT_TOGGLE_PAPER:     togglepaper(arg); break;
	case ACT_PAPER_YELLOW:     paperyellow(arg); break;
	case ACT_TOGGLE_OVERVIEW:   toggle_overview(arg); break;
	case ACT_TOGGLE_MINIMIZED:  toggleminimize(arg); break;
	case ACT_TOGGLE_SCRATCHPAD: togglescratchpad(arg); break;
	case ACT_OPACITY:           opacityadjust(arg); break;
	case ACT_CROP:              cropbegin(arg); break;
	case ACT_CROP_CANCEL:       cropcancel(arg); break;
	case ACT_SCREENSHOT:        capture_screenshot(arg); break;
	case ACT_SCREENSHOT_UI:     screenshotui_begin(arg); break;
	case ACT_CHVT:              chvt(arg); break;
	case ACT_QUIT:              quit(arg); break;
	case ACT_WINDOW_MENU: {
		Client *menu_focus = selmon ? focustop(selmon) : NULL;
		menu_shown = 1;
		/* focustop() doesn't filter panels; a docked panel can hold
		 * keyboard focus (e.g. clicking into it to type). Its geom is
		 * screen-space, not world-space, so feeding it to
		 * viewport_menu_reveal() pans the camera by garbage — see the
		 * ledger entry for this bug. */
		if (menu_focus && !menu_focus->ispanel)
			viewport_menu_reveal(menu_focus);
		ipc_broadcast_state();
		break;
	}
	case ACT_FOCUS_MONITOR: {
		Arg a = {.i = arg->i == 0 ? WLR_DIRECTION_LEFT : WLR_DIRECTION_RIGHT};
		focusmon(&a);
		break;
	}
	case ACT_MOVE_MONITOR: {
		Arg a = {.i = arg->i == 0 ? WLR_DIRECTION_LEFT : WLR_DIRECTION_RIGHT};
		tagmon(&a);
		break;
	}
	case ACT_POINTER_MOVE: {
		Arg a = {.ui = CurMove};
		moveresize(&a);
		break;
	}
	case ACT_POINTER_RESIZE: {
		Arg a = {.ui = CurResize};
		moveresize(&a);
		break;
	}
	case ACT_VIEWPORT_PAN_GRAB: {
		/* Super+Ctrl+LMB: on a normal window, move just that window
		 * without dragging its connection component along; on empty
		 * canvas (or an unmanaged/fullscreen client, same as
		 * pointer-move/-resize elsewhere), pan the camera as before. */
		Client *c = NULL;
		Arg solo = {.ui = CurMoveSolo};
		(void)arg;
		if (!selmon)
			break;
		xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
		if (c && (client_is_unmanaged(c) || c->isfullscreen))
			break;
		if (c) {
			moveresize(&solo);
			break;
		}
		cursor_mode = CurPan;
		viewport_pan_grab_start();
		break;
	}
	default:
		wlr_log(WLR_ERROR, "bind_invoke: unhandled action %d", action_id);
		break;
	}
}

/* Release half of a while-held (hold) action; most actions have no release. */
void
bind_invoke_release(int action_id, const Arg *arg)
{
	(void)arg;
	switch (action_id) {
	case ACT_WINDOW_MENU:
		menu_shown = 0;
		ipc_broadcast_state();
		break;
	default:
		break;
	}
}

int
keybinding(uint32_t mods, xkb_keysym_t sym)
{
	/*
	 * Here we handle compositor keybindings. This is when the compositor is
	 * processing keys, rather than passing them on to the client for its own
	 * processing. Always resolved through the bind DSL (binds_init() already
	 * falls back to the parsed embedded default if the user's file is broken
	 * or missing, so there's no separate compiled-table fallback to keep).
	 */
	return bind_dispatch_key(mods, sym);
}

/* Whether the binding keybinding() most recently matched is safe to
 * auto-repeat while its key is held — checked by keyboard.c before arming the
 * repeat timer. */
int
keybinding_repeatable(void)
{
	return bind_action_is_repeatable(bind_dispatch_last_action());
}

void
killclient(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		client_send_close(sel);
}

/* Round a world coordinate to the placement grid (config placement_grid; 0
 * disables). Applied to automatic placements only — spawn positions and
 * restored persisted positions — never to a drag, so hand-placed windows keep
 * exactly where the user dropped them while everything the compositor decides
 * lands on one pitch. Without this, cursor-centred spawns and years-old saved
 * coordinates gave every window a different arbitrary offset. */
static int
snap_grid(int v)
{
	int g = placement_grid;
	if (g <= 1)
		return v;
	/* Floor-division rounding so negative world coordinates snap the same way
	 * as positive ones (C division truncates toward zero). */
	return (int)(floorf((float)v / (float)g + 0.5f) * (float)g);
}

void
mapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is mapped, or ready to display on-screen. */
	Client *p = NULL;
	Client *w, *c = wl_container_of(listener, c, map);
	Client *old_east = NULL;
	Monitor *m;
	int i;
	int restore_width;
	int restore_height;
	int has_saved_geom = 0;
	/* Snapshot *before* c is inserted into clients/fstack below, and before
	 * anything auto-focuses it (the managed path doesn't focus a new client
	 * inside mapnotify() at all — only the unmanaged branch does, via the
	 * early goto) — so this reliably captures "whichever window was focused
	 * right before this one was created," the spawn-connection graph's
	 * parent and the default spawn-placement anchor (see below). */
	Client *spawn_parent_candidate = focustop(selmon);

	/* Create scene tree for this client and its border */
	c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LyrFloat]);
	if (!c->scene) {
		client_surface(c)->data = NULL;
		wlr_log(WLR_ERROR, "Failed to create scene tree for mapped client");
		return;
	}
	c->id = next_client_id++;
	/* Enabled later by a call to arrange() */
	wlr_scene_node_set_enabled(&c->scene->node, client_is_unmanaged(c));
	c->scene_surface = c->type == XDGShell
			? wlr_scene_xdg_surface_create(c->scene, c->surface.xdg)
			: wlr_scene_subsurface_tree_create(c->scene, client_surface(c));
	c->scene->node.data = c->scene_surface->node.data = c;

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

	/* Float-type windows — a real xdg-shell parent (dialog/transient-for) OR a
	 * fixed-size/modal toplevel (client_is_float_type()) — become screen-space
	 * floating overlays: centered on the parent's on-screen rect (or the
	 * monitor), raised and focused, and exempt from the camera transform (via
	 * c->isfloating, see client_apply_zoom_frame()) so they stay put on top of
	 * the free-positioned canvas windows instead of panning/zooming with it —
	 * the "popup floats, the rest stay put" behavior. They skip rules, cascade,
	 * and the spawn-connection graph. Everything else applies rules and picks a
	 * canvas position. */
	if ((p = client_get_parent(c)) || client_is_float_type(c)) {
		Monitor *fm = p ? p->mon
				: (cursor ? xytomon(cursor->x, cursor->y) : selmon);
		setmon(c, fm ? fm : selmon);
		c->isfloating = 1;
		if (c->mon) {
			int dw = c->geom.width, dh = c->geom.height;
			if (p) {
				c->geom.x = (int)WORLD_TO_SCREEN_X(p->mon,
						p->geom.x + p->geom.width / 2) - dw / 2;
				c->geom.y = (int)WORLD_TO_SCREEN_Y(p->mon,
						p->geom.y + p->geom.height / 2) - dh / 2;
			} else {
				c->geom.x = c->mon->w.x + (c->mon->w.width - dw) / 2;
				c->geom.y = c->mon->w.y + (c->mon->w.height - dh) / 2;
			}
			resize(c, c->geom, 0);
		}
	} else {
		struct wlr_box dockprep_rect = {0};
		int dockprep_matched;

		applyrules(c);
		dockprep_matched = dockprep_consume(client_get_appid(c), &dockprep_rect);
		p = (spawn_parent_candidate && spawn_parent_candidate->mon == c->mon)
				? spawn_parent_candidate : NULL;

		/* Position, in priority order: (0) a pending "dockprep" request
		 * (see its declaration above) — the shell told us in advance this
		 * app_id is about to be docked into an exact rect, so skip straight
		 * to setdocked() instead of picking any floating position at all;
		 * (1) a persisted position from a previous run of this exact app
		 * instance (persistence_register_client() matches by appid+title+
		 * spawn-order — two simultaneously open windows of the same app,
		 * like two plain "foot" terminals, share an appid+title but each
		 * gets its own saved slot via spawn order, so they no longer
		 * collide onto the same saved position — see its comment in
		 * persistence.c); (2) to the right of the spawn parent; (3) a sane
		 * default (monitor center) for the very first window. */
		if (c->mon && dockprep_matched) {
			setdocked(c, 1, dockprep_rect);
		} else if (c->mon) {
			has_saved_geom = persistence_register_client(c);
			if (has_saved_geom) {
				/* persistence_register_client() already applied + resized —
				 * re-snap, since saved coordinates predate the grid (or were
				 * hand-dragged) and would otherwise reintroduce the drift the
				 * grid exists to remove. */
				c->geom.x = snap_grid(c->geom.x);
				c->geom.y = snap_grid(c->geom.y);
				resize(c, c->geom, 0);
			} else if (p) {
				c->geom.x = snap_grid(p->geom.x + p->geom.width + SPAWN_GAP);
				c->geom.y = snap_grid(p->geom.y);
				resize(c, c->geom, 0);

				/* p already has an East neighbor (e.g. focused window is the
				 * leftmost of an existing line) — insert the new window
				 * between them instead of silently failing to connect (what
				 * connect_clients() alone would do, since p's E slot is
				 * taken): sever p<->old_east, shift old_east and everything
				 * still transitively connected to it (the rest of the line)
				 * right to make room, then splice c in between. */
				old_east = p->neighbor[OCT_E];
				if (old_east) {
					Client *component[256];
					int shift = c->geom.width + SPAWN_GAP;
					int ncomp, k;

					p->neighbor[OCT_E] = NULL;
					old_east->neighbor[OCT_W] = NULL;
					ncomp = collect_component(old_east, component, (int)LENGTH(component));
					for (k = 0; k < ncomp; k++) {
						struct wlr_box nb = component[k]->geom;
						nb.x += shift;
						resize(component[k], nb, 0);
					}
				}
			} else if (cursor && c->mon == xytomon(cursor->x, cursor->y)) {
				/* No spawn parent (nothing was focused, or the focused
				 * window is on a different monitor) — center the new window
				 * on the cursor rather than the monitor, so it lands where
				 * the user's attention actually is instead of wherever the
				 * monitor's own geometric middle happens to be. Only when
				 * the cursor is actually on this client's monitor, matching
				 * the (p && p->mon == c->mon) same-monitor guard above. */
				c->geom.x = snap_grid((int)SCREEN_TO_WORLD_X(c->mon, cursor->x) - c->geom.width / 2);
				c->geom.y = snap_grid((int)SCREEN_TO_WORLD_Y(c->mon, cursor->y) - c->geom.height / 2);
				resize(c, c->geom, 0);
			} else {
				c->geom.x = snap_grid(c->mon->w.x + c->mon->w.width / 2 - c->geom.width / 2);
				c->geom.y = snap_grid(c->mon->w.y + c->mon->w.height / 2 - c->geom.height / 2);
				resize(c, c->geom, 0);
			}
		}

		/* Link into the connection graph (same p as the placement anchor
		 * above, same-monitor only, matching setmon() above) — placed to p's
		 * right, so this always connects W/E; connect_clients() computes the
		 * actual octant from the real geometry rather than assuming, and
		 * silently no-ops if a slot is somehow already taken (shouldn't
		 * happen for p's E slot here — old_east above already cleared it —
		 * or for c's W slot, since c is brand new). Skipped for a dockprep
		 * match — a docked panel isn't part of the tiling/connection graph
		 * at all, same as it's exempt from the placement logic above. */
		if (p && !dockprep_matched) {
			connect_clients(p, c);
			if (old_east)
				connect_clients(c, old_east);
		}
	}
	/* Auto-pan to a newly spawned window (viewport.follow_new_windows,
	 * defaults on) — new windows deliberately don't auto-focus (see the
	 * spawn_parent_candidate comment above), so focusclient()'s own
	 * viewport_follow_focus() never fires for them; without this call the
	 * flag was dead and the camera silently never moved to show where a new
	 * window actually landed. Excludes panels (c->ispanel, including the
	 * dockprep case just above): they live in screen space, glued to a
	 * fixed spot regardless of camera position, so "panning to show where
	 * it landed" is meaningless for them and previously made the camera
	 * visibly fly to each docked panel's first-ever spawn. */
	/* A floating overlay is screen-space and already on top where the user is
	 * looking — panning the camera to it is meaningless (like a panel). */
	/* A window restored from persistence (has_saved_geom) also mustn't pull
	 * the camera: createmon() already restored the saved camera to the exact
	 * view that framed these windows, and centering on each respawned window
	 * as it maps would undo that and leave the camera on whichever restored
	 * window happened to map last. */
	if (c->mon && c->mon->cam.follow_new_windows && !c->ispanel && !c->isfloating
			&& !has_saved_geom)
		viewport_center_on(c);
	ftl_create(c);
	/* The scene node was created disabled (line ~2405, "enabled later by a
	 * call to arrange()") and setmon()->setfullscreen()'s already-correctly-
	 * parented fast path (this client's node starts parented under its final
	 * layer already) skips the arrange_mark_dirty() call it would otherwise
	 * make — so nothing else schedules the arrange() that actually flips
	 * enabled to true. Without this, a newly mapped window stays invisible
	 * until some unrelated event happens to dirty this monitor. */
	if (c->mon)
		arrange_mark_dirty(c->mon);
	/* Modal/dialog overlays (a sudo prompt, a file picker) must grab focus so
	 * the user can act on them immediately — unlike ordinary spawned windows,
	 * which deliberately don't steal focus (see spawn_parent_candidate). The
	 * lift raises it to the top of its layer, above the canvas windows. */
	if (c->isfloating && c->mon)
		focusclient(c, 1);
	status_mark_dirty();

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

	/* Mirror docked-client hover transitions to the shell over IPC (see
	 * dock_hover_client's declaration) — only on actual enter/leave, not
	 * every motion tick, since this runs on every pointer sample and a
	 * status broadcast on every pixel of mouse movement would flood the
	 * socket for no reason. */
	{
		Client *now_hover = (c && c->docked) ? c : NULL;
		if (now_hover != dock_hover_client) {
			dock_hover_client = now_hover;
			status_mark_dirty();
		}
	}

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

		if (active_constraint && cursor_mode != CurResize && cursor_mode != CurMove && cursor_mode != CurMoveSolo) {
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

		/* While a menu-armed connect (Super+L) is pending, the shell needs
		 * the live cursor position every tick to draw the rubber-band line
		 * (see ipc_build_state()'s "pending_connect"). Gated on the pending
		 * state itself so this doesn't add per-motion IPC traffic in the
		 * common (nothing armed) case. */
		if (connect_pick_pending())
			status_mark_dirty();
	}

	/* Update drag icon's position */
	wlr_scene_node_set_position(&drag_icon->node, (int)round(cursor->x), (int)round(cursor->y));

	/* Drag-to-cut: re-test the *current* cursor position against every live
	 * connection line each tick, for the rest of the CurCut drag armed in
	 * buttonpress() — see the comment there for why sweeping is more
	 * forgiving than a single precise click. */
	if (cursor_mode == CurCut) {
		uint32_t id_a, id_b;
		if (connection_click_hit(cursor->x, cursor->y, &id_a, &id_b))
			sever_connection(id_a, id_b);
		return;
	}

	if ((cursor_mode == CurMove || cursor_mode == CurMoveSolo || cursor_mode == CurResize) && !grabc) {
		cursor_mode = CurNormal;
		return;
	}

	/* If we are currently grabbing the mouse, handle and return.
	 * geom is in world space; the cursor is in screen space, so convert via
	 * SCREEN_TO_WORLD (which accounts for pan + zoom). grabcx/grabcy hold the
	 * grab offset in world units (see moveresize()). */
	if ((cursor_mode == CurMove || cursor_mode == CurMoveSolo) && grabc) {
		Monitor *drag_mon = xytomon(cursor->x, cursor->y);
		int old_x, old_y, new_x, new_y, move_dx, move_dy;
		int handed_off = 0;

		/* Drag hand-off (multi-camera): when the cursor crosses onto another
		 * monitor mid-drag, reassign the holder so the transform below goes
		 * through the camera actually under the cursor. No re-basing math is
		 * needed — grabcx/grabcy are window-internal world offsets, so the
		 * SCREEN_TO_WORLD below re-derives a world position that puts the
		 * grabbed point back under the cursor through the *new* camera.
		 * Camera-bypassed clients (docked/fullscreen/maximized — screen-space
		 * geometry, see client_apply_zoom_frame()) keep the old release-time
		 * setmon() in buttonpress() instead. Edges into the old monitor are
		 * severed: a connection line through two different cameras has no
		 * coherent geometry (see sever_cross_monitor_edges()). */
		if (drag_mon && grabc->mon && drag_mon != grabc->mon
				&& !grabc->docked && !grabc->isfullscreen && !grabc->ismaximized) {
			setmon(grabc, drag_mon);
			sever_cross_monitor_edges(grabc);
			handed_off = 1;
		}

		old_x = grabc->geom.x;
		old_y = grabc->geom.y;
		new_x = (int)lroundf(SCREEN_TO_WORLD_X(grabc->mon, cursor->x)) - grabcx;
		new_y = (int)lroundf(SCREEN_TO_WORLD_Y(grabc->mon, cursor->y)) - grabcy;
		move_dx = new_x - old_x;
		move_dy = new_y - old_y;

		/* Move the grabbed client to the new position. */
		resize(grabc, (struct wlr_box){
			.x = new_x, .y = new_y,
			.width = grabc->geom.width, .height = grabc->geom.height}, 1);

		/* Drag the whole spawn-connection component along: every window
		 * transitively reachable from grabc via an unsevered connection
		 * moves by the same screen-space delta — but gliding after grabc
		 * (spring-animated, like a tether) rather than snapping instantly,
		 * unlike grabc itself which stays pinned exactly under the cursor.
		 * Based off target_geom (not geom) when a member is still mid-glide,
		 * so repeated small deltas during one continuous drag accumulate
		 * correctly instead of compounding against a stale animated position.
		 * Skipped entirely for CurMoveSolo (Super+Ctrl+LMB): that mode moves
		 * just the grabbed window, leaving its connections intact but not
		 * dragging the rest of the component along. Also skipped on the
		 * hand-off tick: move_dx/dy then include the camera re-basing jump,
		 * not cursor motion, and any surviving (same-monitor) component
		 * member would be flung by it. */
		if (cursor_mode == CurMove && !handed_off && (move_dx || move_dy)) {
			Client *component[256];
			int n = collect_component(grabc, component, (int)LENGTH(component));
			int i;
			for (i = 0; i < n; i++) {
				int base_x, base_y;
				if (component[i] == grabc)
					continue;
				base_x = component[i]->animating ? component[i]->target_geom.x : component[i]->geom.x;
				base_y = component[i]->animating ? component[i]->target_geom.y : component[i]->geom.y;
				client_set_target_geom(component[i], (struct wlr_box){
					.x = base_x + move_dx,
					.y = base_y + move_dy,
					.width = component[i]->geom.width,
					.height = component[i]->geom.height});
			}
		}
		return;
	} else if (cursor_mode == CurResize && grabc) {
		/* resize_anchor_x/y (set once in moveresize()) is the fixed corner
		 * opposite whichever one was grabbed; the grabbed corner tracks the
		 * cursor, so the anchor is always the min and the size is always
		 * the distance to it, regardless of which of the 4 corners this is. */
		int wx = (int)lroundf(SCREEN_TO_WORLD_X(grabc->mon, cursor->x));
		int wy = (int)lroundf(SCREEN_TO_WORLD_Y(grabc->mon, cursor->y));
		resize(grabc, (struct wlr_box){
			.x = MIN(wx, resize_anchor_x), .y = MIN(wy, resize_anchor_y),
			.width = abs(wx - resize_anchor_x),
			.height = abs(wy - resize_anchor_y)}, 1);
		return;
	} else if (cursor_mode == CurPan) {
		viewport_pan_grab_update();
		return;
	}

	/* Crop mode: update selection rectangle */
	if (crop_editor.active && crop_editor.dragging) {
		crop_editor.end_x = cursor->x;
		crop_editor.end_y = cursor->y;
		cropdraw();
		return;
	}

	/* Screenshot UI: update selection rectangle while dragging */
	if (screenshot_ui.active && screenshot_ui.dragging) {
		screenshot_ui.end_x = cursor->x;
		screenshot_ui.end_y = cursor->y;
		screenshotui_draw();
		return;
	}

	/* Screenshot UI: fade the info panel as the cursor approaches, even
	 * while just hovering (not dragging) — screenshotui_draw() above only
	 * runs mid-drag, so this is the only per-motion hook while idle. Falls
	 * through (no return) so normal pointer-focus handling below still runs. */
	if (screenshot_ui.active)
		screenshotui_hover();

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

	switch (cursor_mode = arg->ui) {
	case CurMove:
	case CurMoveSolo:
		/* Offset stored in world units (cursor is screen space). Solo move
		 * (Super+Ctrl+LMB) uses the exact same offset math as a normal move
		 * — the only difference is motionnotify() skips dragging the rest
		 * of the connection component along. */
		grabcx = (int)lroundf(SCREEN_TO_WORLD_X(grabc->mon, cursor->x)) - grabc->geom.x;
		grabcy = (int)lroundf(SCREEN_TO_WORLD_Y(grabc->mon, cursor->y)) - grabc->geom.y;
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "all-scroll");
		break;
	case CurResize: {
		/* Grab whichever corner of the window is nearest the cursor, not
		 * always the bottom-right one: the opposite corner is the fixed
		 * anchor for the whole drag, and the grabbed corner is warped to
		 * meet the cursor so the resize starts exactly where you clicked. */
		int wx = (int)lroundf(SCREEN_TO_WORLD_X(grabc->mon, cursor->x));
		int wy = (int)lroundf(SCREEN_TO_WORLD_Y(grabc->mon, cursor->y));
		int cx = grabc->geom.x + grabc->geom.width / 2;
		int cy = grabc->geom.y + grabc->geom.height / 2;
		int grab_x, grab_y;
		const char *xcursor;

		resize_anchor_x = wx < cx ? grabc->geom.x + grabc->geom.width : grabc->geom.x;
		resize_anchor_y = wy < cy ? grabc->geom.y + grabc->geom.height : grabc->geom.y;
		grab_x = resize_anchor_x == grabc->geom.x ? grabc->geom.x + grabc->geom.width : grabc->geom.x;
		grab_y = resize_anchor_y == grabc->geom.y ? grabc->geom.y + grabc->geom.height : grabc->geom.y;

		if (grab_x == grabc->geom.x)
			xcursor = grab_y == grabc->geom.y ? "nw-resize" : "sw-resize";
		else
			xcursor = grab_y == grabc->geom.y ? "ne-resize" : "se-resize";

		/* Doesn't work for X11 output - the next absolute motion event
		 * returns the cursor to where it started. Warp target is screen space. */
		wlr_cursor_warp_closest(cursor, NULL,
				WORLD_TO_SCREEN_X(grabc->mon, grab_x), WORLD_TO_SCREEN_Y(grabc->mon, grab_y));
		wlr_cursor_set_xcursor(cursor, cursor_mgr, xcursor);
		break;
	}
	}
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
		} else {
			printf("%s title \n", m->wlr_output->name);
			printf("%s appid \n", m->wlr_output->name);
			printf("%s fullscreen \n", m->wlr_output->name);
		}

		printf("%s selmon %u\n", m->wlr_output->name, m == selmon);

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
			printf("%s win %s %s geom %d %d %d %d\n",
				m->wlr_output->name, appid, title,
				c->geom.x, c->geom.y, c->geom.width, c->geom.height);
		}
	}

	/* Output per-monitor camera state for debugging and status bars */
	wl_list_for_each(m, &mons, link) {
		if (!m->wlr_output)
			continue;
		printf("viewport %s %.0f %.0f %.2f follow %s follow_new %s\n",
			m->wlr_output->name,
			m->cam.x, m->cam.y, m->cam.zoom,
			m->cam.follow ? "on" : "off",
			m->cam.follow_new_windows ? "on" : "off");
	}
	
	/* Output crop mode state */
	printf("crop %s\n", crop_editor.active ? "active" : "inactive");

	fflush(stdout);

	/* Mirror focus/fullscreen state to foreign-toplevel handles for shells. */
	ftl_sync_state();

	/* Push viewport/compositor state to any connected IPC shell clients. */
	ipc_broadcast_state();
}

static int
exit_confirm_expire(void *data)
{
	(void)data;
	/* The confirmation window lapsed without a second press: reset so the next
	 * arming is a fresh 0->1 edge for the shell prompt. */
	exit_confirm.pending = 0;
	exit_pending = 0;
	ipc_broadcast_state();
	return 0;
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
		/* Surface the pending confirmation so the shell shows an
		 * on-screen "press Esc again to quit" prompt, and auto-clear it when
		 * the window lapses so a later arming re-flashes. */
		exit_pending = 1;
		ipc_broadcast_state();
		if (event_loop) {
			if (!exit_confirm_timer)
				exit_confirm_timer = wl_event_loop_add_timer(event_loop,
						exit_confirm_expire, NULL);
			if (exit_confirm_timer)
				wl_event_source_timer_update(exit_confirm_timer,
						EXIT_CONFIRMATION_SECONDS * 1000);
		}
	} else {
		/* Second press within timeout - actually exit */
		wlr_log(WLR_INFO, "Exit confirmed - quitting dwl");
		exit_pending = 0;
		if (exit_confirm_timer)
			wl_event_source_timer_update(exit_confirm_timer, 0);
		/* Safety net for positions that changed some other way than a
		 * direct drag-release (which already persists immediately) — e.g.
		 * a group-drag member that wasn't the directly-grabbed client. */
		persistence_save();
		wl_display_terminate(dpy);
	}
}

#include "modules/ui/offscreen_indicators.c"
#include "modules/spawn/tmux_spawn.c"
#include "modules/output/output.c"
#include "modules/dock/dockprep.c"

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
	/* Keep frames coming on this output for as long as the camera is still
	 * easing toward its target — no separate polling timer; viewport_kick()
	 * (viewport_ops.c) only needs to request the *first* one to start this
	 * self-sustaining chain. */
	if (m->cam.animating)
		wlr_output_schedule_frame(m->wlr_output);
	/* Step window spring-glide in the frame callback so it is vsync-aligned with
	 * the camera; keep frames coming while anything is still moving. */
	if (clients_anim_step())
		wlr_output_schedule_frame(m->wlr_output);
	offscreen_indicators_update();

	/* wlr_scene_surface resets each buffer's dest_size to the surface's native
	 * size on every surface commit, which clobbers the zoom applied in resize().
	 * Re-apply it here — after commits, just before rendering — so window
	 * *content* scales with the camera, not just the frame. */
	wl_list_for_each(c, &clients, link) {
		if (client_is_rendered_on_mon(c, m)) {
			client_set_buffer_scale(c, MON_ZOOM_SAFE(c->mon));
			/* Same reset-on-commit problem as buffer scale above: a cropped
			 * client's clip must be reapplied every frame or it reverts to the
			 * full, uncropped surface as soon as the client commits again. */
			client_apply_crop_clip(c);
			/* Paper mode: re-shade the window and reinject the overlay after
			 * the buffer scale/clip for this frame are settled. Gated so plain
			 * windows pay nothing. */
			if (c->paper_mode || c->paper_node)
				client_apply_paper(c);
		}
	}

	/* Previously this skipped the ENTIRE monitor's commit whenever any tiled,
	 * visible client had an outstanding (un-acked) resize — e.g. every newly
	 * spawned window, from the moment arrange_columns() assigns it a size
	 * until its process starts, connects, and commits a matching buffer. On
	 * a cold process start that ack can take a very visible amount of time,
	 * during which NOTHING on the monitor rendered — not other windows, not
	 * the cursor — a hard freeze, followed by every backed-up scene change
	 * landing in one commit once the slow client finally caught up (a visible
	 * "teleport"). Not skipping just means a not-yet-acked client's old
	 * buffer gets stretched into its new
	 * box for a frame or two — a barely-visible blip on that one window,
	 * instead of a frozen screen. */
	/* Route the frame through the shader pipeline (offscreen render + a
	 * fragment-shader output pass) when shaders are enabled and available;
	 * otherwise, and on any failure, fall back to the plain scene commit.
	 * See obsidian/plan/shaders.md. */
	if (!shaders_render_output(m))
		wlr_scene_output_commit(m->scene_output, NULL);

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

/* Position the client's content within its frame and, if cropped, clip it to
 * the selected sub-region (offset + wlr_scene_subsurface_tree_set_clip).
 * wlroots resets a subsurface tree's clip on every surface commit (the same
 * reset client_set_buffer_scale works around for dest_size — see its comment),
 * so this must be re-applied every frame in rendermon(), not just once from
 * resize(), or the crop silently reverts to the full, uncropped surface as
 * soon as the client commits again. */
static void
client_apply_crop_clip(Client *c)
{
	struct wlr_box clip;
	int base_inner_w, base_inner_h;
	int src_x, src_y, vis_w, vis_h;
	int full_clip_w, full_clip_h;
	float zf;
	int z_bw;

	if (!c || !c->scene_surface)
		return;

	zf = MON_ZOOM_SAFE(c->mon);
	z_bw = (int)lroundf(c->bw * zf);

	client_get_clip(c, &clip);

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

		/* Clip subtree to selected source region (source/buffer coords).
		 * client_set_buffer_scale() already scales dest_size down to match this
		 * region (vis_w/vis_h, via the crop.w/crop.h fraction), so the clipped
		 * region displays natively at the frame origin — no extra position
		 * shift needed (an old per-pixel shift here, sized for full-scale
		 * display, is stale now that dest_size shrinks with the crop and
		 * pushed the content out from under the frame). */
		clip.x += src_x;
		clip.y += src_y;
		clip.width = vis_w;
		clip.height = vis_h;
	}

	if (c->scene_surface->node.x != z_bw || c->scene_surface->node.y != z_bw)
		wlr_scene_node_set_position(&c->scene_surface->node, z_bw, z_bw);

	/* Re-issuing an identical clip every frame (this runs unconditionally from
	 * rendermon()) turned out to matter: on real hardware it correlated with a
	 * cropped client's commit rate spiking into the thousands/sec and the whole
	 * session eventually locking up (GPU hang) — see the 2026-07 ledger entry.
	 * Skip the call entirely when nothing changed. */
	if (!c->crop.clip_cached || c->crop.last_clip_x != clip.x || c->crop.last_clip_y != clip.y
			|| c->crop.last_clip_w != clip.width || c->crop.last_clip_h != clip.height) {
		wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
		c->crop.last_clip_x = clip.x;
		c->crop.last_clip_y = clip.y;
		c->crop.last_clip_w = clip.width;
		c->crop.last_clip_h = clip.height;
		c->crop.clip_cached = true;
	}
}

void
resize(Client *c, struct wlr_box geo, int interact)
{
	struct wlr_box *bbox;
	int cfg_w, cfg_h;
	int unbounded;

	if (!c || !c->mon || !client_surface(c)->mapped || !c->scene || !c->scene_surface)
		return;

	/* Borders may not be created yet during early setup */
	if (!c->border[0] || !c->border[1] || !c->border[2] || !c->border[3])
		return;

	bbox = interact ? &sgeom : &c->mon->w;
	/* Every window lives in world space and may extend beyond the monitor
	 * viewport — that's the whole point of the infinite canvas — except a
	 * live interactive drag/resize, which clamps to the physical screen
	 * (sgeom) while it's happening. Fullscreen always clamps (it must land
	 * exactly on the monitor's own region for wlr_scene_output to show it). */
	unbounded = !interact && !c->isfullscreen;

	client_set_bounds(c, geo.width, geo.height);
	c->geom = geo;

	/* Keep minimum size constraints, but don't clamp x/y to monitor bounds. */
	if (unbounded) {
		c->geom.width = MAX(1 + 2 * (int)c->bw, c->geom.width);
		c->geom.height = MAX(1 + 2 * (int)c->bw, c->geom.height);
	} else if (interact && !c->isfullscreen) {
		/* c->geom is world-space (pannable/zoomable), but `sgeom` is fixed
		 * screen-pixel space — comparing them directly (as applybounds()
		 * does) means any window more than one screen-width from world
		 * (0,0) reads as "off past the edge" and gets its x/y stomped back
		 * into the tiny 0..sgeom.width/height range the instant you drag
		 * it, regardless of where it actually was. Convert sgeom into the
		 * world-space rect currently under the camera before clamping, so
		 * a drag only gets bounded by what's actually visible on screen
		 * right now, not by raw screen-pixel numbers misread as world
		 * coordinates. (Fullscreen is exempt: its geom is deliberately
		 * output-layout space already, not pannable world space — see
		 * client_apply_zoom_frame()'s matching bypass.) */
		float zf = MON_ZOOM_SAFE(c->mon);
		/* Per-monitor cameras: bound the drag to the world region visible
		 * on the window's own monitor, not the whole layout union. */
		struct wlr_box vis = c->mon ? c->mon->m : sgeom;
		struct wlr_box world_bbox = {
			.x = (int)lroundf(SCREEN_TO_WORLD_X(c->mon, vis.x)),
			.y = (int)lroundf(SCREEN_TO_WORLD_Y(c->mon, vis.y)),
			.width = (int)lroundf((float)vis.width / zf),
			.height = (int)lroundf((float)vis.height / zf),
		};
		applybounds(c, &world_bbox);
	} else {
		applybounds(c, bbox);
	}

	if (!c->crop.active) {
		c->crop.base_w = c->geom.width;
		c->crop.base_h = c->geom.height;
		c->crop.saved_base = true;
	}

	/* Apply viewport transform (world -> screen, includes zoom) and size the
	 * frame's border/focus-ring for the current zoom — shared with
	 * viewport_camera_tick()'s every-frame camera-only refresh, see
	 * client_apply_zoom_frame(). The client itself always stays configured at
	 * its native logical size (cfg_w/cfg_h below) — only the displayed frame
	 * and the buffer's dest_size (client_set_buffer_scale()) scale with zoom. */
	client_apply_zoom_frame(c);

	/* True crop keeps the client configured at its base (uncropped) size —
	 * only the displayed region is restricted, via client_apply_crop_clip(). */
	if (c->crop.active && c->crop.saved_base
			&& c->crop.base_w > 2 * (int)c->bw && c->crop.base_h > 2 * (int)c->bw) {
		cfg_w = c->crop.base_w - 2 * (int)c->bw;
		cfg_h = c->crop.base_h - 2 * (int)c->bw;
	} else {
		cfg_w = c->geom.width - 2 * (int)c->bw;
		cfg_h = c->geom.height - 2 * (int)c->bw;
	}

	cfg_w = MAX(1, cfg_w);
	cfg_h = MAX(1, cfg_h);

	/* this is a no-op if size hasn't changed */
	c->resize = client_set_size(c, cfg_w, cfg_h);
	client_apply_crop_clip(c);

	/* Scale the displayed buffer to match the zoomed frame. */
	client_set_buffer_scale(c, MON_ZOOM_SAFE(c->mon));
}

/* Helper to check if a float is close to an integer */
static int
is_integer_zoom(float zoom)
{
	float rounded = roundf(zoom);
	return fabsf(zoom - rounded) < 0.01f;
}

/* Recursively scale every buffer under a scene node to (scale_w, scale_h) of
 * its natural size, and scale child offsets so subsurfaces stay aligned.
 * wlr_scene_xdg_surface_create nests the real surface buffer below
 * scene_surface, so a direct-children-only pass never found it — that was the
 * "only the frame zooms" bug.
 *
 * scale_w/scale_h are independent (not just a uniform zoom) so a cropped
 * client can be handled in the same pass: wlr_scene_subsurface_tree_set_clip's
 * clip selects a source sub-rect, but wlr_scene_buffer_set_dest_size then
 * scales *that clipped sub-rect* to fill dest_size — so if dest_size were left
 * at the full uncropped surface size, the small cropped region would be
 * stretched up to fill it (the "crop zooms/stretches" bug). Multiplying scale
 * by the crop fraction (client_set_buffer_scale) makes dest_size match the
 * clipped region's own size instead. */
/* Recover a subsurface scene node's *native* (unscaled) offset from wlroots
 * state, so the zoom scaling below reads native -> screen every frame instead
 * of re-multiplying the value it wrote on the previous frame.
 *
 * The old code did `node->x = round(node->x * scale)` every frame. wlr_scene
 * only resets a subsurface node's position back to native on the subsurface's
 * own commit, so between commits (static content while the camera zooms) that
 * multiply compounds — the offset drifts and eventually explodes, which is what
 * made window internals resize/jitter when zoomed (visible in screenshots, and
 * as flicker on subsurface-heavy clients like Zen during the overview zoom).
 *
 * wlr_scene positions each subsurface as a child *tree* at
 * `wlr_subsurface.current.x/y`; that tree's own surface is its direct scene
 * buffer child. Returns 0 (caller falls back to the current, possibly-scaled
 * value — never worse than before) when the node isn't a resolvable subsurface. */
static int
subsurface_native_offset(struct wlr_scene_node *node, int *out_x, int *out_y)
{
	struct wlr_surface *surface = NULL;
	struct wlr_subsurface *sub;

	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *sb = wlr_scene_buffer_from_node(node);
		struct wlr_scene_surface *ss = sb ? wlr_scene_surface_try_from_buffer(sb) : NULL;
		surface = ss ? ss->surface : NULL;
	} else if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *ch;
		wl_list_for_each(ch, &tree->children, link) {
			struct wlr_scene_buffer *sb;
			struct wlr_scene_surface *ss;
			if (ch->type != WLR_SCENE_NODE_BUFFER)
				continue;
			sb = wlr_scene_buffer_from_node(ch);
			ss = sb ? wlr_scene_surface_try_from_buffer(sb) : NULL;
			if (ss && ss->surface) {
				surface = ss->surface;
				break;
			}
		}
	}

	if (!surface)
		return 0;
	sub = wlr_subsurface_try_from_wlr_surface(surface);
	if (!sub)
		return 0;
	*out_x = sub->current.x;
	*out_y = sub->current.y;
	return 1;
}

static void
client_scale_buffers(struct wlr_scene_node *node, float scale_w, float scale_h, float out_scale)
{
	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *sb = wlr_scene_buffer_from_node(node);
		struct wlr_scene_surface *ss;
		int dw, dh;
		if (!sb || !sb->buffer || sb->buffer->width <= 0 || sb->buffer->height <= 0)
			return;
		/* dest_size is in layout (logical) coords, so it must be logical_size *
		 * zoom — independent of how many pixels the client rendered. That keeps
		 * it correct once the client renders at zoom DPI (crisp, not upscaled). */
		ss = wlr_scene_surface_try_from_buffer(sb);
		if (ss && ss->surface && ss->surface->current.width > 0) {
			dw = MAX(1, (int)lroundf(ss->surface->current.width  * scale_w));
			dh = MAX(1, (int)lroundf(ss->surface->current.height * scale_h));
		} else {
			/* Plain buffer (no surface): fall back to pixels / output scale. */
			float fw = scale_w / (out_scale > 0.0f ? out_scale : 1.0f);
			float fh = scale_h / (out_scale > 0.0f ? out_scale : 1.0f);
			dw = MAX(1, (int)lroundf(sb->buffer->width  * fw));
			dh = MAX(1, (int)lroundf(sb->buffer->height * fh));
		}
		/* This runs every frame for every rendered client (see rendermon()); a
		 * dest_size set with an unchanged value still turned out to matter on
		 * real hardware (see client_apply_crop_clip()'s clip cache above), so
		 * skip the call when nothing changed. */
		if (sb->dst_width != dw || sb->dst_height != dh)
			wlr_scene_buffer_set_dest_size(sb, dw, dh);
		{
			enum wlr_scale_filter_mode want = (is_integer_zoom(scale_w) && is_integer_zoom(scale_h))
					? WLR_SCALE_FILTER_NEAREST : WLR_SCALE_FILTER_BILINEAR;
			if (sb->filter_mode != want)
				wlr_scene_buffer_set_filter_mode(sb, want);
		}
	} else if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each(child, &tree->children, link) {
			/* Scale a subsurface's offset from the primary surface (the
			 * primary sits at 0,0 so it is unaffected). Read the *native*
			 * offset from wlroots when we can (idempotent — see
			 * subsurface_native_offset()); otherwise fall back to the current
			 * node position, which preserves the pre-fix behavior. */
			int base_x = child->x, base_y = child->y;
			int native_x, native_y;
			/* Resolve native *before* the zero-check: a subsurface whose
			 * scaled offset rounded to 0 on a prior frame must still be
			 * rescaled from its true native offset when the zoom changes
			 * back, not treated as unoffset and skipped forever. */
			if (subsurface_native_offset(child, &native_x, &native_y)) {
				base_x = native_x;
				base_y = native_y;
			}
			if (base_x || base_y) {
				int nx = (int)lroundf(base_x * scale_w);
				int ny = (int)lroundf(base_y * scale_h);
				if (nx != child->x || ny != child->y)
					wlr_scene_node_set_position(child, nx, ny);
			}
			client_scale_buffers(child, scale_w, scale_h, out_scale);
		}
	}
}

/* Scale the window's buffer to implement zoom at the content level: window
 * content grows/shrinks with the camera, not just the frame. Must be re-applied
 * each frame in rendermon() because wlr_scene_surface resets dest_size on every
 * surface commit.
 *
 * When cropped, the scale is additionally reduced by the crop fraction (see
 * client_scale_buffers) so the clipped sub-region displays at its own native
 * size (times zoom) instead of being stretched to fill the full surface. */
void
client_set_buffer_scale(Client *c, float scale)
{
	float out_scale, scale_w, scale_h;
	if (!c || !c->scene_surface)
		return;
	/* Fullscreen (and maximized — same reasoning) content is never subject
	 * to camera zoom — see the matching bypass in client_apply_zoom_frame(). */
	if (client_is_camera_bypassed(c))
		scale = 1.0f;
	if (scale <= 0.0f)
		scale = 1.0f;
	out_scale = (c->mon && c->mon->wlr_output) ? c->mon->wlr_output->scale : 1.0f;
	scale_w = scale_h = scale;
	if (c->crop.active && c->crop.saved_base
			&& c->crop.base_w > 2 * (int)c->bw && c->crop.base_h > 2 * (int)c->bw) {
		scale_w = scale * c->crop.w;
		scale_h = scale * c->crop.h;
	}
	client_scale_buffers(&c->scene_surface->node, scale_w, scale_h, out_scale);
}

/* Paper mode's reinjected overlay must be invisible to input hit-testing:
 * xytonode() dereferences wlr_scene_surface_try_from_buffer() on whatever
 * wlr_scene_node_at() returns, so a plain (non-surface) buffer sitting on top
 * would both steal the hit and crash on the NULL surface. Returning false makes
 * wlr_scene_node_at() skip the overlay and fall through to the real surface
 * underneath — which stays enabled — so a papered window keeps pointer and
 * keyboard input. */
static bool
paper_node_rejects_input(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	return false;
}

/* Bounding box (absolute layout coords) of the buffer nodes under a client's
 * scene subtree — the same box shaders_window_shade() renders into, recomputed
 * here so the shaded overlay can be positioned exactly over its content. Mirrors
 * shaders.c's bounds pass; wlr_scene_node_for_each_buffer() skips rect and
 * disabled nodes, so borders/focus rings and last frame's (disabled) overlay
 * are excluded, as intended. */
struct paper_bounds {
	int x0, y0, x1, y1;
	bool any;
};

static void
paper_bounds_iter(struct wlr_scene_buffer *sb, int sx, int sy, void *data)
{
	struct paper_bounds *b = data;
	int w, h;

	if (!sb || !sb->buffer)
		return;
	if (sb->dst_width > 0 && sb->dst_height > 0) {
		w = sb->dst_width;
		h = sb->dst_height;
	} else {
		w = sb->buffer->width;
		h = sb->buffer->height;
	}
	if (w <= 0 || h <= 0)
		return;
	if (!b->any) {
		b->x0 = sx;
		b->y0 = sy;
		b->x1 = sx + w;
		b->y1 = sy + h;
		b->any = true;
		return;
	}
	if (sx < b->x0)
		b->x0 = sx;
	if (sy < b->y0)
		b->y0 = sy;
	if (sx + w > b->x1)
		b->x1 = sx + w;
	if (sy + h > b->y1)
		b->y1 = sy + h;
}

/* Drive paper mode for one client each frame (from rendermon()): render the
 * window's subtree through paper.frag and reinject the shaded copy as an overlay
 * above the surface, or tear the overlay down when paper mode is off. The real
 * surface stays enabled underneath (occluded by the opaque overlay) so input
 * routing is unaffected. See obsidian/plan/shaders.md and the paper-shader-core
 * API in shaders.h. */
static void
client_apply_paper(Client *c)
{
	struct shader_paper_params params, base;
	struct wlr_buffer *shaded;
	struct paper_bounds b = {0};
	int lx, ly;

	if (!c || !c->scene || !c->scene_surface)
		return;

	if (!c->paper_mode) {
		/* Toggled off (or off from the start): drop any overlay and release
		 * the offscreen buffers. shaders_window_release() is a no-op for a
		 * client that was never shaded. */
		if (c->paper_node) {
			wlr_scene_buffer_set_buffer(c->paper_node, NULL);
			wlr_scene_node_destroy(&c->paper_node->node);
			c->paper_node = NULL;
		}
		shaders_window_release(c);
		return;
	}

	/* Exclude last frame's overlay from the capture: shaders_window_shade()
	 * and the bounds pass below both walk c->scene, and a disabled node is
	 * skipped — so the effect never feeds back on its own output. */
	if (c->paper_node)
		wlr_scene_node_set_enabled(&c->paper_node->node, false);

	/* config paper_* is the yellow=1 endpoint; the papyrus knob eases strength
	 * and warms the page toward paper_aged as c->paper_yellow rises. */
	base.strength = paper_strength;
	base.paper[0] = paper_color[0];
	base.paper[1] = paper_color[1];
	base.paper[2] = paper_color[2];
	base.ink[0] = paper_ink[0];
	base.ink[1] = paper_ink[1];
	base.ink[2] = paper_ink[2];
	base.preserve = paper_preserve;
	params = ws_paper_from_yellow(c->paper_yellow, &base, paper_aged);

	shaded = shaders_window_shade(c, &params);
	if (!shaded)
		/* Shaders unavailable (Pixman/Vulkan) or a transient failure: leave
		 * the window unshaded this frame (overlay stays disabled). */
		return;

	/* Recompute the captured box and express its origin relative to c->scene
	 * so the overlay lands 1:1 over the content it was rendered from (the
	 * shaded buffer maps 1:1 to this box in layout units). */
	wlr_scene_node_for_each_buffer(&c->scene->node, paper_bounds_iter, &b);
	if (!b.any || !wlr_scene_node_coords(&c->scene->node, &lx, &ly))
		return;

	if (!c->paper_node) {
		c->paper_node = wlr_scene_buffer_create(c->scene, NULL);
		if (!c->paper_node)
			return;
		c->paper_node->point_accepts_input = paper_node_rejects_input;
		wlr_scene_node_place_above(&c->paper_node->node,
				&c->scene_surface->node);
	}
	wlr_scene_buffer_set_buffer(c->paper_node, shaded);
	/* Opacity just under 1.0 marks the overlay non-opaque for occlusion, so
	 * wlr_scene keeps the window's own surface visible on the output. A fully
	 * opaque overlay occludes it, wlroots sends wl_surface.leave, and the client
	 * stops rendering (no more frames to shade). The <1% of the raw window that
	 * blends through is imperceptible. */
	wlr_scene_buffer_set_opacity(c->paper_node, 0.99f);
	wlr_scene_node_set_position(&c->paper_node->node, b.x0 - lx, b.y0 - ly);
	wlr_scene_node_set_enabled(&c->paper_node->node, true);
}

/* Ask each mapped client to render at zoom DPI so zoomed content is crisp, not
 * upscaled. Called from viewport_tick() when the camera *settles* (not every
 * frame) to avoid a re-render storm; clients that honor wp_fractional_scale
 * (foot, most GTK/Qt) re-render at the higher resolution. */
void
client_apply_zoom_scale(void)
{
	Client *c;

	/* Per-monitor cameras: each client renders at its holder's zoom DPI, so
	 * there's no single "applied" zoom to shortcut on — client_set_scale()
	 * itself no-ops when the surface is already at the target scale, which
	 * keeps the settle-time call cheap. */
	wl_list_for_each(c, &clients, link) {
		float out_scale, target;
		struct wlr_surface *s = client_surface(c);
		if (!c->mon || !s || !s->mapped)
			continue;
		out_scale = c->mon->wlr_output ? c->mon->wlr_output->scale : 1.0f;
		target = out_scale * MON_ZOOM_SAFE(c->mon);
		if (target < out_scale)        target = out_scale;      /* never below native */
		if (target > zoom_render_max)  target = zoom_render_max;
		client_set_scale(s, target);
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
	binds_init();
	persistence_init();

	/* Bootstrap the persistent tmux session every spawn()/spawn_named()
	 * launch becomes a window in (see spawn_named() above) — doing it here,
	 * after WAYLAND_DISPLAY/KALIN_IPC_SOCKET are set above, means the tmux
	 * *server's* environment (captured at session-creation time, then
	 * reused for every window it ever creates) has both set correctly, so
	 * GUI apps launched into it can actually reach this compositor. Harmless
	 * if the session already exists (a previous compositor run's session
	 * outlives this process — tmux's server is a separate long-lived
	 * daemon): new-session fails with "duplicate session", which is exactly
	 * what we want, just discarded rather than logged as an error. */
	{
		pid_t tpid = fork();
		if (tpid == 0) {
			int devnull = open("/dev/null", O_WRONLY);
			if (devnull >= 0) {
				dup2(devnull, STDOUT_FILENO);
				dup2(devnull, STDERR_FILENO);
			}
			setsid();
			execlp("tmux", "tmux", "new-session", "-d", "-s", "kalin-apps", NULL);
			_exit(1);
		} else if (tpid > 0) {
			waitpid(tpid, NULL, 0);
		} else {
			wlr_log(WLR_ERROR, "Failed to fork for tmux session bootstrap: %s", strerror(errno));
		}
	}

	/* Session resurrection: relaunch last run's saved clients into the
	 * kalin-apps session just bootstrapped above (its environment must
	 * already carry this compositor's WAYLAND_DISPLAY, so this cannot move
	 * earlier). Best-effort — see persistence_respawn_saved(). */
	persistence_respawn_saved();

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

	status_mark_dirty();

	/* At this point the outputs are initialized: place the cursor in the
	 * centre of the layout (warp_closest clamps into the nearest output if
	 * the midpoint falls in a dead gap between monitors), pick selmon from
	 * where it landed, and set the default image. The inherited hack warped
	 * the cursor to its own backend position — a move that
	 * wlr_output_cursor_move() discards as a no-op — so the image just
	 * appeared wherever the hardware had it until the first real motion. */
	wlr_cursor_warp_closest(cursor, NULL,
			sgeom.x + sgeom.width / 2.0, sgeom.y + sgeom.height / 2.0);
	selmon = xytomon(cursor->x, cursor->y);
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
setfullscreen(Client *c, int fullscreen)
{
	int was_fullscreen = c->isfullscreen;
	struct wlr_scene_tree *want_layer;

	c->isfullscreen = fullscreen;
	if (!c->mon || !client_surface(c)->mapped)
		return;

	/* setmon() re-asserts c->isfullscreen on every window spawn regardless of
	 * whether anything is actually changing — skip the steady "not
	 * fullscreen, staying not fullscreen" case (already correctly parented).
	 * Every window always has a real position by the time this runs
	 * (mapnotify() places it before any of this fires), so unlike the old
	 * tiled-layout version of this check there's no "not placed yet" case to
	 * special-case any more. */
	want_layer = layers[c->isfullscreen ? LyrFS : (c->isontop ? LyrFloatTop : LyrFloat)];
	if (!fullscreen && !was_fullscreen && c->scene->node.parent == want_layer)
		return;

	c->bw = fullscreen ? 0 : borderpx;
	client_set_fullscreen(c, fullscreen);
	wlr_scene_node_reparent(&c->scene->node, want_layer);

	if (fullscreen) {
		c->prev = c->geom;
		resize(c, c->mon->m, 0);
	} else {
		/* restore previous size/position instead of recalculating, since
		 * every window's position is now always user/persistence-owned. */
		resize(c, c->prev, 0);
	}
	/* Refreshes m->fullscreen_bg's enabled state (arrange() ties it to
	 * focustop(m)->isfullscreen) — the one piece of arrange() a fullscreen
	 * transition genuinely still needs. */
	arrange_mark_dirty(c->mon);
	status_mark_dirty();
}

/* Fill the monitor's usable work area (c->mon->w: excludes layer-shell
 * reserved space like bars, unlike setfullscreen()'s c->mon->m) — not
 * fullscreen (no LyrFS reparent, no bar/border hiding). Snapshots/restores
 * c->premax (geometry only — every window is already free-positioned, so
 * there's no floating/tiled detour to manage any more, unlike this
 * function's earlier version). */
void
setmaximized(Client *c, int maximized)
{
	if (!c || c->ismaximized == maximized || c->isfullscreen)
		return;
	c->ismaximized = maximized;

	if (!c->mon || !client_surface(c)->mapped)
		return;

	if (maximized) {
		c->premax = c->geom;
		resize(c, c->mon->w, 0);
	} else {
		resize(c, c->premax, 0);
	}
	status_mark_dirty();
}

/* Pin/unpin a window "always on top": stays visually above every other
 * window regardless of subsequent focus elsewhere (unlike the default, where
 * focusclient()'s raise-to-top only reorders siblings within LyrFloat, so
 * focusing any other window covers whatever was focused before). Applies to
 * any window now (there's no floating/tiled distinction left to gate it on). */
static void
setontop(Client *c, int ontop)
{
	struct wlr_scene_tree *want_layer;

	if (!c || c->isontop == ontop)
		return;
	c->isontop = ontop;

	if (c->isfullscreen)
		return;
	if (!c->mon || !client_surface(c)->mapped)
		return;

	want_layer = layers[c->isontop ? LyrFloatTop : LyrFloat];
	wlr_scene_node_reparent(&c->scene->node, want_layer);
	status_mark_dirty();
}

/* Hide/show a client without touching its wl_surface: the process stays alive,
 * VISIBLEON() (kalin.h) skips it in arrange/focustop scans, and c->geom is
 * left untouched so restoring lands back at the same canvas spot. */
void
setminimized(Client *c, int minimized)
{
	if (!c || c->minimized == minimized)
		return;
	c->minimized = minimized;
	if (c->foreign_toplevel)
		wlr_foreign_toplevel_handle_v1_set_minimized(c->foreign_toplevel, minimized);
	if (minimized)
		focus_top(selmon, 1); /* was possibly focused; redirect to the new top */
	else
		focusclient(c, 1);
	if (c->mon)
		arrange_mark_dirty(c->mon);
	status_mark_dirty();
}

void
toggleminimize(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel && !sel->isfullscreen)
		setminimized(sel, !sel->minimized);
}

/* Look up a mapped client by exact app_id match. Shared by togglescratchpad()
 * (find-or-spawn) and the IPC dock/undock commands (ipc.c) — a shell panel
 * addresses a docked client by app_id rather than a numeric id since it's the
 * one spawning it and already knows the app_id it chose. */
Client *
client_find_by_appid(const char *appid)
{
	Client *c;

	if (!appid)
		return NULL;
	wl_list_for_each(c, &clients, link) {
		const char *id = client_get_appid(c);
		if (id && !strcmp(id, appid))
			return c;
	}
	return NULL;
}

/* Pin a client into a shell-panel-owned screen rect: borderless, exempt from
 * the world/camera transform (see client_apply_zoom_frame()'s matching
 * bypass), geometry driven by whatever `rect` the IPC "dock" command last
 * sent rather than the user dragging/resizing it. Mirrors setfullscreen()'s
 * shape (save/restore c->prev, force bw, reparent, resize) but keeps the
 * client in LyrFloatTop rather than LyrFS — a docked terminal is meant to
 * coexist with the desktop around it, not take over the whole output.
 *
 * Docking in glides up into place via the spring-glide system
 * (client_set_target_geom()/client_anim.c) instead of teleporting, to read
 * as a reveal — like the shell's own SidePanel drawer — rather than a
 * window just appearing. The glide's *start* point is synthesized as
 * directly below `rect` (same x/width/height, y at the rect's bottom edge —
 * right at the bar), not the client's actual pre-dock position: that
 * position is in world space (wherever it happened to be floating), while
 * `rect` is screen space, and interpolating raw numbers between two
 * different coordinate systems would produce a nonsensical diagonal swoop
 * instead of a clean slide. Undocking doesn't animate — every caller pairs
 * it with an immediate minimize (see the IPC "undock"/"minimize" commands),
 * so nothing is ever on screen to see it glide. */
void
setdocked(Client *c, int docked, struct wlr_box rect)
{
	if (!c || c->docked == docked)
		return;
	if (!c->mon || !client_surface(c)->mapped)
		return;

	c->bw = docked ? 0 : borderpx;

	if (docked) {
		struct wlr_box start = rect;
		start.y = rect.y + rect.height;

		c->prev = c->geom;
		c->docked = 1;
		/* Tag it as a panel forever, not just for this docked spell — see
		 * ispanel's declaration in kalin.h for why `docked` alone (which
		 * goes false again during the undock-then-minimize window on
		 * close) isn't enough for taskbar/camera/overview exclusion. A
		 * client that's ever been docked once is chrome for the rest of
		 * its life. */
		if (!c->ispanel) {
			c->ispanel = 1;
			ftl_destroy(c);
		}
		wlr_scene_node_reparent(&c->scene->node, layers[LyrFloatTop]);

		c->anim_ready = 0; /* force the next call to snap to `start` first */
		client_set_target_geom(c, start);
		client_set_target_geom(c, rect);
	} else {
		c->docked = 0;
		wlr_scene_node_reparent(&c->scene->node, layers[LyrFloat]);
		resize(c, c->prev, 0);
	}
	arrange_mark_dirty(c->mon);
	status_mark_dirty();
}

void
setmon(Client *c, Monitor *m)
{
	Monitor *oldmon = c->mon;

	if (oldmon == m)
		return;
	c->mon = m;
	c->prev = c->geom;

	/* Scene graph sends surface leave/enter events on move and resize.
	 * Skip while unmapped: commitnotify()'s initial_commit calls
	 * setmon(c, selmon) then immediately setmon(c, NULL) on the same not-yet-
	 * visible client (so mapnotify() can reapply rules) — arranging oldmon
	 * here would be a full sweep over every other window for a client that
	 * was never part of the visible tiling in the first place. */
	if (oldmon && client_surface(c)->mapped)
		arrange_mark_dirty(oldmon);
	if (m) {
		/* Make sure window actually overlaps with the monitor */
		resize(c, c->geom, 0);
		setfullscreen(c, c->isfullscreen); /* This will mark c->mon dirty */
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
	offscreen_indicators_init();

	/* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
	 * can also specify a renderer using the WLR_RENDERER env var.
	 * The renderer is responsible for defining the various pixel formats it
	 * supports for shared memory, this configures that for clients. */
	if (!(drw = wlr_renderer_autocreate(backend)))
		die("couldn't create renderer");
	wl_signal_add(&drw->events.lost, &gpu_reset);

	/* Set up the shader subsystem now that the renderer exists. Disables
	 * itself gracefully on a non-GLES2 renderer or shader-compile failure —
	 * see obsidian/plan/shaders.md. */
	shaders_init(drw, shaders_output_enabled, shaders_dir);

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
	toplevel_export_init(dpy);
	toplevel_icon_init(dpy);
	system_bell_init(dpy);
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

	session_lock_init();

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

/* One unnamed scratchpad terminal: first toggle spawns it (it self-floats via
 * its dedicated app_id Rule), later toggles hide/show the same process instead
 * of killing it. Looked up by app_id rather than a cached pointer so a process
 * that actually exits and gets relaunched is picked up cleanly. */
void
togglescratchpad(const Arg *arg)
{
	Client *found = client_find_by_appid("kalin-scratchpad");

	if (found)
		setminimized(found, !found->minimized);
	else
		spawn(arg);
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
	Monitor *m;

	/* dirtomon() dereferences selmon, so only ask for a target once we know
	 * there's a focused client (which implies a live selmon) to send. */
	if (!sel || !selmon)
		return;
	m = dirtomon(arg->i);
	if (!m || m == sel->mon)
		return;
	setmon(sel, m);
	/* Send-to-monitor (multi-camera): the old world position is arbitrary
	 * through the new holder's camera, so teleport to the center of what
	 * that camera currently shows. Camera-bypassed clients (docked/
	 * fullscreen/maximized) keep their screen-space geometry — setmon()'s
	 * own resize/setfullscreen already relocates those. Cross-camera edges
	 * are severed for the same reason as the drag hand-off in
	 * motionnotify(); the new position is persisted like a drag drop is. */
	if (!sel->isfullscreen && !sel->ismaximized && !sel->docked) {
		int cwx = (int)lroundf(SCREEN_TO_WORLD_X(m, m->m.x + m->m.width / 2.0));
		int cwy = (int)lroundf(SCREEN_TO_WORLD_Y(m, m->m.y + m->m.height / 2.0));
		resize(sel, (struct wlr_box){
			.x = cwx - sel->geom.width / 2, .y = cwy - sel->geom.height / 2,
			.width = sel->geom.width, .height = sel->geom.height}, 0);
	}
	sever_cross_monitor_edges(sel);
	persistence_save();
}


void
togglefullscreen(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		setfullscreen(sel, !sel->isfullscreen);
}

void
togglemaximized(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel && !sel->isfullscreen)
		setmaximized(sel, !sel->ismaximized);
}

void
toggleontop(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel && !sel->isfullscreen)
		setontop(sel, !sel->isontop);
}

/* Toggle whether the focused window is allowed to overlap its
 * connection-graph neighbors (see resolve_growth_overlap()). Purely a flag
 * flip: there's no layer/geometry change, unlike setontop()/setmaximized(). */
void
toggleoverlap(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel) {
		sel->allow_overlap = !sel->allow_overlap;
		status_mark_dirty();
	}
}

/* Toggle the paper-mode reading tint on the focused window. The flag is read
 * every frame by client_apply_paper() (rendermon()), which builds or tears down
 * the shaded overlay; schedule a frame so the change shows immediately even when
 * the output is otherwise idle. */
static void
togglepaper(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (!sel)
		return;
	sel->paper_mode = !sel->paper_mode;
	/* Enabling on a window that has never been aged lands on the default
	 * papyrus knob so the effect is visible; the paper-yellow bind ramps
	 * from there. Leave a hand-tuned yellow untouched across off/on cycles. */
	if (sel->paper_mode && sel->paper_yellow <= 0.0f)
		sel->paper_yellow = paper_yellow_default;
	if (sel->mon && sel->mon->wlr_output)
		wlr_output_schedule_frame(sel->mon->wlr_output);
	/* Push the new yellow so the shell's papyrus gauge reflects it live. */
	ipc_broadcast_state();
}

/* Ramp the focused window's papyrus knob by arg->f (clamped to [0,1]) and keep
 * paper_mode in sync — ramping up from nothing turns the effect on, ramping to
 * zero turns it off. Read every frame by client_apply_paper(); schedule a frame
 * so the change shows on an otherwise idle output. */
static void
paperyellow(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (!sel || !arg)
		return;
	sel->paper_yellow = fmaxf(0.0f, fminf(1.0f, sel->paper_yellow + arg->f));
	sel->paper_mode = sel->paper_yellow > 0.0f;
	if (sel->mon && sel->mon->wlr_output)
		wlr_output_schedule_frame(sel->mon->wlr_output);
	/* Push the new yellow so the shell's papyrus gauge tracks the ramp live. */
	ipc_broadcast_state();
}

void
unmaplayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, unmap);

	/* See the matching log in destroylayersurfacenotify() — unmap can
	 * happen without a destroy (client asked to unmap, or is about to
	 * remap), so this is the event that actually fires when a client like
	 * quickshell drops off the Wayland connection uncleanly. */
	wlr_log(WLR_INFO, "layer-shell surface unmapped: namespace=\"%s\" layer=%d",
			l->layer_surface->namespace ? l->layer_surface->namespace : "(null)",
			l->layer_surface->pending.layer);

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
	/* A pending Super+L connect (see connect_pick_arm()) holds a raw
	 * Client* between arming and the completing click — cancel it if the
	 * armed window itself closes in that window, or the pointer would
	 * dangle. */
	if (c == connect_pick_pending())
		connect_pick_cancel();

	/* Same dangling-pointer concern as connect_pick_pending() above: clear
	 * the hover mirror if the client that unmapped is the one it points at,
	 * and tell the shell so it doesn't keep a docked panel considered
	 * "hovered" after the client is gone. */
	if (c == dock_hover_client) {
		dock_hover_client = NULL;
		status_mark_dirty();
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

		/* Connection graph cleanup: first splice each pair of opposite
		 * neighbors (N<->S, NE<->SW, E<->W, SE<->NW) directly together if
		 * both sides of that axis exist — removing the middle of a line
		 * should join what's left into one line again, not leave a gap
		 * with two dangling ends. connect_clients() computes the real
		 * octant from current geometry and no-ops if a slot's already
		 * taken, so this is safe to attempt even when it doesn't quite
		 * apply (e.g. the two neighbors are also connected some other
		 * way already). Then detach every one of our up-to-8 neighbors
		 * symmetrically (sever, not reparent otherwise — the simplest,
		 * safest choice, matching "sever" already being a one-edge cut
		 * elsewhere). */
		{
			int i, j;
			for (i = 0; i < 4; i++) {
				Client *a = c->neighbor[i];
				Client *b = c->neighbor[i + 4];
				if (a && b) {
					/* a's and b's slots pointing back at c (by symmetry,
					 * the opposite octant of the one c uses for each)
					 * are still set at this point — connect_clients()
					 * treats an occupied slot as "already connected to
					 * something else" and silently no-ops, so it would
					 * always refuse to link a<->b while they still each
					 * point back at the client that's leaving. Clear
					 * just those two back-pointers first. */
					int opp_a = opposite_octant(i);
					int opp_b = opposite_octant(i + 4);
					if (a->neighbor[opp_a] == c)
						a->neighbor[opp_a] = NULL;
					if (b->neighbor[opp_b] == c)
						b->neighbor[opp_b] = NULL;
					connect_clients(a, b);
					close_gap(a, b);
				}
			}
			for (i = 0; i < 8; i++) {
				Client *n = c->neighbor[i];
				if (!n)
					continue;
				for (j = 0; j < 8; j++)
					if (n->neighbor[j] == c)
						n->neighbor[j] = NULL;
				c->neighbor[i] = NULL;
			}
		}

		persistence_unregister_client(c);

		if (c->link.prev && c->link.next)
			wl_list_remove(&c->link);
		setmon(c, NULL);
		if (c->flink.prev && c->flink.next)
			wl_list_remove(&c->flink);
	}

	/* Paper mode: detach the borrowed shaded buffer from its overlay node
	 * before the scene tree (which owns the node) is torn down, then release
	 * the client's offscreen buffers. Safe when the client was never papered. */
	if (c->paper_node) {
		wlr_scene_buffer_set_buffer(c->paper_node, NULL);
		c->paper_node = NULL;
	}
	shaders_window_release(c);

	wlr_scene_node_destroy(&c->scene->node);
	status_mark_dirty();
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
updatetitle(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_title);
	ftl_update_title(c);
	if (c == focustop(c->mon))
		status_mark_dirty();
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
	status_mark_dirty();

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

		if (node->type == WLR_SCENE_NODE_BUFFER) {
			/* Plain pixel buffers (e.g. screenshot UI's info readout, paper
			 * mode's reinjected overlay) aren't Wayland surfaces, so
			 * try_from_buffer() returns NULL here — must check before
			 * dereferencing ->surface. */
			struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(
					wlr_scene_buffer_from_node(node));
			if (ss)
				surface = ss->surface;
		}
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
