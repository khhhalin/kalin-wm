/* Minimal unix-domain IPC for external shells (e.g. quickshell).
 *
 * foreign-toplevel-management already covers window enumeration/control; this
 * socket exposes the things it cannot: the infinite-canvas camera (viewport)
 * and compositor-wide state, plus camera commands a panel/gesture can send.
 *
 * Protocol: newline-delimited.
 *   - Server -> client: one JSON object per line, emitted on every state change
 *     (driven by printstatus()), e.g.
 *       {"type":"state","viewport":{...},"crop":false,"focused":{...}}
 *   - Client -> server: plain text commands, one per line:
 *       pan <dx> <dy>     move the camera (world units)
 *       zoom <factor>     multiply zoom (e.g. 1.1 / 0.9)
 *       zoom-reset        reset camera to origin / 1.0
 *       follow-toggle     toggle camera-follows-focus
 *
 * The socket path is exported via $KALIN_IPC_SOCKET. This file is #include'd
 * into dwl.c and uses its globals (dpy, event_loop, viewport, selmon, ...). */

#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>

#define IPC_MAX_CLIENTS 16
#define IPC_BUF_SIZE    4096

struct ipc_client {
	int fd;
	struct wl_event_source *source;
};

static int ipc_listen_fd = -1;
static struct wl_event_source *ipc_listen_source;
static struct ipc_client ipc_clients[IPC_MAX_CLIENTS];
static char ipc_socket_path[256];

static void
ipc_client_remove(struct ipc_client *cl)
{
	if (cl->source) {
		wl_event_source_remove(cl->source);
		cl->source = NULL;
	}
	if (cl->fd >= 0) {
		close(cl->fd);
		cl->fd = -1;
	}
}

/* Escape a string for safe inclusion inside a JSON double-quoted value. */
static void
ipc_json_escape(const char *in, char *out, size_t outlen)
{
	size_t o = 0;
	if (!in)
		in = "";
	for (; *in && o + 2 < outlen; in++) {
		unsigned char ch = (unsigned char)*in;
		if (ch == '"' || ch == '\\') {
			out[o++] = '\\';
			out[o++] = ch;
		} else if (ch >= 0x20) {
			out[o++] = ch;
		}
		/* drop control characters */
	}
	out[o] = '\0';
}

static void
ipc_build_state(char *buf, size_t len)
{
	Client *f = selmon ? focustop(selmon) : NULL;
	char title[512];
	char appid[256];

	ipc_json_escape(f ? client_get_title(f) : "", title, sizeof(title));
	ipc_json_escape(f ? client_get_appid(f) : "", appid, sizeof(appid));

	snprintf(buf, len,
		"{\"type\":\"state\","
		"\"viewport\":{\"x\":%.0f,\"y\":%.0f,\"zoom\":%.3f,"
		"\"follow\":%s,\"follow_new\":%s},"
		"\"crop\":%s,"
		"\"focused\":{\"appid\":\"%s\",\"title\":\"%s\",\"fullscreen\":%s}}\n",
		viewport.x, viewport.y, viewport.zoom,
		viewport.follow ? "true" : "false",
		viewport.follow_new_windows ? "true" : "false",
		crop_editor.active ? "true" : "false",
		appid, title,
		(f && f->isfullscreen) ? "true" : "false");
}

static void
ipc_send(int fd, const char *msg)
{
	size_t len = strlen(msg);
	ssize_t n = write(fd, msg, len);
	(void)n; /* best-effort; client fds are non-blocking */
}

static void
ipc_broadcast_state(void)
{
	char raw[IPC_BUF_SIZE];
	int i;
	if (ipc_listen_fd < 0)
		return;
	ipc_build_state(raw, sizeof(raw));
	for (i = 0; i < IPC_MAX_CLIENTS; i++) {
		if (ipc_clients[i].fd < 0)
			continue;
		if (write(ipc_clients[i].fd, raw, strlen(raw)) < 0
				&& errno != EAGAIN && errno != EINTR)
			ipc_client_remove(&ipc_clients[i]);
	}
}

static void
ipc_exec_command(char *line)
{
	char *save = NULL;
	char *cmd = strtok_r(line, " \t\r", &save);
	if (!cmd)
		return;

	if (strcmp(cmd, "pan") == 0) {
		char *sx = strtok_r(NULL, " \t\r", &save);
		char *sy = strtok_r(NULL, " \t\r", &save);
		float d[2] = { sx ? (float)atof(sx) : 0.0f, sy ? (float)atof(sy) : 0.0f };
		Arg a = {.v = d};
		viewport_pan(&a);
	} else if (strcmp(cmd, "zoom") == 0) {
		char *sf = strtok_r(NULL, " \t\r", &save);
		Arg a = {.f = sf ? (float)atof(sf) : 1.0f};
		viewport_zoom(&a);
	} else if (strcmp(cmd, "zoom-reset") == 0) {
		Arg a = {0};
		viewport_reset(&a);
	} else if (strcmp(cmd, "follow-toggle") == 0) {
		Arg a = {0};
		viewport_toggle_follow(&a);
	} else {
		wlr_log(WLR_DEBUG, "ipc: unknown command '%s'", cmd);
	}
}

static int
ipc_client_readable(int fd, uint32_t mask, void *data)
{
	struct ipc_client *cl = data;
	char buf[IPC_BUF_SIZE];
	char *save = NULL;
	char *line;
	ssize_t n;

	if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
		ipc_client_remove(cl);
		return 0;
	}

	n = read(fd, buf, sizeof(buf) - 1);
	if (n <= 0) {
		if (n == 0 || (errno != EAGAIN && errno != EINTR))
			ipc_client_remove(cl);
		return 0;
	}
	buf[n] = '\0';

	for (line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
		ipc_exec_command(line);
	return 0;
}

static int
ipc_handle_accept(int fd, uint32_t mask, void *data)
{
	char initial[IPC_BUF_SIZE];
	int cfd, i, flags;
	(void)mask; (void)data;

	cfd = accept(fd, NULL, NULL);
	if (cfd < 0)
		return 0;

	for (i = 0; i < IPC_MAX_CLIENTS; i++)
		if (ipc_clients[i].fd < 0)
			break;
	if (i == IPC_MAX_CLIENTS) {
		wlr_log(WLR_ERROR, "ipc: too many clients, rejecting");
		close(cfd);
		return 0;
	}

	flags = fcntl(cfd, F_GETFL, 0);
	if (flags >= 0)
		fcntl(cfd, F_SETFL, flags | O_NONBLOCK);

	ipc_clients[i].fd = cfd;
	ipc_clients[i].source = wl_event_loop_add_fd(event_loop, cfd,
			WL_EVENT_READABLE, ipc_client_readable, &ipc_clients[i]);

	/* Greet the new client with the current state. */
	ipc_build_state(initial, sizeof(initial));
	ipc_send(cfd, initial);
	return 0;
}

static void
ipc_init(const char *wl_display_name)
{
	struct sockaddr_un addr = {0};
	const char *rundir = getenv("XDG_RUNTIME_DIR");
	int i;

	for (i = 0; i < IPC_MAX_CLIENTS; i++)
		ipc_clients[i].fd = -1;

	if (!rundir)
		rundir = "/tmp";
	snprintf(ipc_socket_path, sizeof(ipc_socket_path), "%s/kalin-ipc-%s.sock",
			rundir, wl_display_name ? wl_display_name : "0");

	if (strlen(ipc_socket_path) >= sizeof(addr.sun_path)) {
		wlr_log(WLR_ERROR, "ipc: socket path too long: %s", ipc_socket_path);
		return;
	}

	unlink(ipc_socket_path);
	ipc_listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (ipc_listen_fd < 0) {
		wlr_log(WLR_ERROR, "ipc: socket() failed: %s", strerror(errno));
		return;
	}

	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", ipc_socket_path);
	if (bind(ipc_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		wlr_log(WLR_ERROR, "ipc: bind(%s) failed: %s", ipc_socket_path, strerror(errno));
		close(ipc_listen_fd);
		ipc_listen_fd = -1;
		return;
	}
	if (listen(ipc_listen_fd, 4) < 0) {
		wlr_log(WLR_ERROR, "ipc: listen() failed: %s", strerror(errno));
		close(ipc_listen_fd);
		ipc_listen_fd = -1;
		return;
	}

	ipc_listen_source = wl_event_loop_add_fd(event_loop, ipc_listen_fd,
			WL_EVENT_READABLE, ipc_handle_accept, NULL);
	setenv("KALIN_IPC_SOCKET", ipc_socket_path, 1);
	wlr_log(WLR_INFO, "ipc: listening on %s", ipc_socket_path);
}

static void
ipc_finish(void)
{
	int i;
	for (i = 0; i < IPC_MAX_CLIENTS; i++)
		if (ipc_clients[i].fd >= 0)
			ipc_client_remove(&ipc_clients[i]);
	if (ipc_listen_source) {
		wl_event_source_remove(ipc_listen_source);
		ipc_listen_source = NULL;
	}
	if (ipc_listen_fd >= 0) {
		close(ipc_listen_fd);
		ipc_listen_fd = -1;
	}
	if (ipc_socket_path[0])
		unlink(ipc_socket_path);
}
