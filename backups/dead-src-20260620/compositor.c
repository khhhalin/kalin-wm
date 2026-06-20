#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "../include/kalin.h"
#include "../include/compositor.h"
#include "../include/viewport.h"
#include "../include/crash_report.h"

/* Global state */
struct wl_display *dpy;
struct wl_event_loop *event_loop;
struct wlr_backend *backend;
struct wlr_renderer *drw;
struct wlr_allocator *alloc;
struct wlr_scene *scene;
struct wlr_xdg_shell *xdg_shell;
struct wlr_layer_shell_v1 *layer_shell;
struct wlr_output_layout *output_layout;
struct wlr_seat *seat;
struct wlr_xwayland *xwayland;

Atom netatom[NetLast];

/* Session lock */
struct wlr_session_lock_manager_v1 *session_lock_mgr;
struct wl_listener new_session_lock;
struct wl_list locks;
struct wl_list locked_monitors;
int locked;
struct wlr_scene_rect *locked_bg;

/* Local state */
static int running = 1;

/* Event listeners */
static struct wl_listener backend_new_input = {.notify = inputdevice};
static struct wl_listener backend_new_output = {.notify = createmon};
static struct wl_listener backend_request_gamma = {.notify = gammacontrol};
static struct wl_listener layout_change = {.notify = updatemons};
static struct wl_listener xdg_shell_new_surface = {.notify = createnotify};
static struct wl_listener layer_shell_new_surface = {.notify = createlayersurface};

#ifdef XWAYLAND
static struct wl_listener xwayland_ready = {.notify = xwaylandready};
#endif

void
handlesig(int sig)
{
	if (sig == SIGCHLD) {
		/* Reap children and notify PTY subsystem for cleanup */
		int status;
		pid_t pid;
		while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
			pty_child_reaped(pid);
		}
	} else if (sig == SIGINT || sig == SIGTERM) {
		running = 0;
		crash_report_write("Graceful shutdown", sig);
		wl_display_terminate(dpy);
	} else if (sig == SIGSEGV || sig == SIGABRT || sig == SIGFPE) {
		crash_report_write("Fatal signal received", sig);
		/* Exit so ly can restart */
		_exit(1);
	}
}

void
setup(void)
{
	int drm_fd;
	int sig[] = {SIGCHLD, SIGINT, SIGTERM, SIGPIPE, SIGSEGV, SIGABRT, SIGFPE};
	struct sigaction sa = {.sa_flags = SA_RESTART, .sa_handler = handlesig};
	sigemptyset(&sa.sa_mask);

	/* Initialize crash reporting first */
	crash_reporting_init();
	if (crash_check_safe_mode()) {
		fprintf(stderr, "WARNING: safe mode enabled due to repeated crashes\n");
	}

	/* Set up signal handlers */
	for (int i = 0; i < (int)LENGTH(sig); i++)
		sigaction(sig[i], &sa, NULL);

	wlr_log_init(log_level, NULL);

	/* Create Wayland display */
	dpy = wl_display_create();
	event_loop = wl_display_get_event_loop(dpy);

	/* Create backend */
	backend = wlr_backend_autocreate(event_loop, &session);
	if (!backend)
		die("couldn't create backend");

	/* Create scene graph */
	scene = wlr_scene_create();
	
	/* Create renderer and allocator */
	drw = wlr_renderer_autocreate(backend);
	if (!drw)
		die("couldn't create renderer");
	
	wlr_renderer_init_wl_shm(drw, dpy);
	
	if (wlr_renderer_get_texture_formats(drw, WLR_BUFFER_CAP_DMABUF)) {
		wlr_drm_create(dpy, drw);
		wlr_scene_set_linux_dmabuf_v1(scene,
			wlr_linux_dmabuf_v1_create_with_renderer(dpy, 5, drw));
	}

	alloc = wlr_allocator_autocreate(backend, drw);
	if (!alloc)
		die("couldn't create allocator");

	/* Create scene layers */
	for (int i = 0; i < NUM_LAYERS; i++)
		layers[i] = wlr_scene_tree_create(&scene->tree);

	/* Create compositor */
	wlr_compositor_create(dpy, 5, drw);
	wlr_subcompositor_create(dpy);
	wlr_data_device_manager_create(dpy);

	/* Create output layout */
	output_layout = wlr_output_layout_create(dpy);
	wl_signal_add(&output_layout->events.change, &layout_change);
	wlr_scene_attach_output_layout(scene, output_layout);

	/* Create xdg shell */
	xdg_shell = wlr_xdg_shell_create(dpy, 5);
	wl_signal_add(&xdg_shell->events.new_surface, &xdg_shell_new_surface);

	/* Create layer shell */
	layer_shell = wlr_layer_shell_v1_create(dpy, 4);
	wl_signal_add(&layer_shell->events.new_surface, &layer_shell_new_surface);

	/* Create seat */
	seat = wlr_seat_create(dpy, "seat0");

	/* Set up cursor */
	cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(cursor, output_layout);
	cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

	/* Set up input */
	wl_signal_add(&backend->events.new_input, &backend_new_input);
	wl_signal_add(&backend->events.new_output, &backend_new_output);

	/* Various protocols */
	wlr_data_control_manager_v1_create(dpy);
	wlr_export_dmabuf_manager_v1_create(dpy);
	wlr_fractional_scale_manager_v1_create(dpy, 1);
	wlr_gamma_control_manager_v1_create(dpy);
	wlr_idle_inhibit_v1_create(dpy);
	wlr_idle_notify_v1_create(dpy);
	wlr_output_manager_v1_create(dpy);
	wlr_output_power_manager_v1_create(dpy);
	wlr_primary_selection_v1_device_manager_create(dpy);
	wlr_viewporter_create(dpy);
	wlr_single_pixel_buffer_manager_v1_create(dpy);
	wlr_presentation_create(dpy, backend);
	wlr_alpha_modifier_v1_create(dpy);

	/* XDG activation */
	activation = wlr_xdg_activation_v1_create(dpy);
	wl_signal_add(&activation->events.request_activate, &request_activate);

	/* XDG decoration */
	wlr_xdg_decoration_manager_v1_create(dpy);

	/* Virtual input */
	wlr_virtual_keyboard_manager_v1_create(dpy);
	wlr_virtual_pointer_manager_v1_create(dpy);

	/* Session lock */
	session_lock_mgr = wlr_session_lock_manager_v1_create(dpy);
	wl_list_init(&locks);
	wl_list_init(&locked_monitors);
	locked = 0;
	wl_signal_add(&session_lock_mgr->events.new_lock, &new_session_lock);

	/* Pointer constraints */
	pointer_constraints = wlr_pointer_constraints_v1_create(dpy);
	wl_signal_add(&pointer_constraints->events.new_constraint, &new_pointer_constraint);

#ifdef XWAYLAND
	/* XWayland */
	xwayland = wlr_xwayland_create(dpy, compositor, 1);
	if (xwayland) {
		wl_signal_add(&xwayland->events.ready, &xwayland_ready);
		wl_signal_add(&xwayland->events.new_surface, &xwayland_surface);
		setenv("DISPLAY", xwayland->display_name, 1);
	}
#endif

	/* Initialize viewport */
	viewport_init();
	persistence_init();
}

void
run(char *startup_cmd)
{
	const char *socket = wl_display_add_socket_auto(dpy);
	if (!socket)
		die("startup: display_add_socket_auto");
	setenv("WAYLAND_DISPLAY", socket, 1);

	if (!wlr_backend_start(backend))
		die("startup: backend_start");

	/* Run startup command */
	if (startup_cmd) {
		int piperw[2];
		if (pipe(piperw) < 0)
			die("startup: pipe");
		
		pid_t pid = fork();
		if (pid < 0)
			die("startup: fork");
		
		if (pid == 0) {
			/* Child */
			dup2(piperw[0], STDIN_FILENO);
			close(piperw[0]);
			close(piperw[1]);
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
			_exit(1);
		}
		close(piperw[0]);
		close(piperw[1]);
	}

	/* Main event loop */
	wl_display_run(dpy);
}

void
cleanup(void)
{
	/* Cleanup in reverse order */
	persistence_save();
	persistence_cleanup();
	wallpaper_cleanup();
	
#ifdef XWAYLAND
	if (xwayland)
		wlr_xwayland_destroy(xwayland);
#endif

	wlr_allocator_destroy(alloc);
	wlr_renderer_destroy(drw);
	wlr_backend_destroy(backend);
	wlr_display_destroy(dpy);
}

void
quit(const Arg *arg)
{
	(void)arg;
	running = 0;
	wl_display_terminate(dpy);
}

int
is_running(void)
{
	return running;
}
