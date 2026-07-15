/* Minimal unix-domain IPC for external shells (e.g. quickshell).
 *
 * foreign-toplevel-management already covers window enumeration/control; this
 * socket exposes the things it cannot: the infinite-canvas camera (viewport)
 * and compositor-wide state, plus camera commands a panel/gesture can send.
 *
 * Protocol: newline-delimited.
 *   - Server -> client: one JSON object per line, emitted on every state change
 *     (driven by printstatus()), e.g.
 *       {"type":"state","viewport":{...},"crop":false,"focused":{...},
 *        "dock_hover":"<appid>"|null,"outputs":[...],"brightness":{...}|null}
 *     "dock_hover" is the app_id of whichever docked client (see the "dock"
 *     command below) the cursor is currently over, or null — lets a panel
 *     auto-hide a docked terminal on cursor-leave, which it has no other way
 *     to observe since a docked client is a real toplevel, not QML content.
 *     "outputs" is every connected monitor's current state + full mode
 *     list: [{"name":"LVDS-1","x":0,"y":0,"width":1600,"height":900,
 *     "refresh":60.057,"scale":1.0,"enabled":true,
 *     "modes":[{"width":1600,"height":900,"refresh":60.057},...]},...] — lets
 *     a shell panel (e.g. the display-settings TUI) read available
 *     resolutions/refresh rates and current mode/scale/position without
 *     shelling out to wlr-randr (see "set-output" below for changing them).
 *     "brightness" is {"value":<raw>,"max":<raw>} for the backlight device
 *     (see backlight.c), or null if none was found (e.g. a desktop with no
 *     built-in panel) — see "set-brightness" below for changing it.
 *   - Client -> server: plain text commands, one per line:
 *       pan <dx> <dy>     move the camera (world units)
 *       zoom <factor>     multiply zoom (e.g. 1.1 / 0.9)
 *       zoom-reset        reset camera to origin / 1.0
 *       follow-toggle     toggle camera-follows-focus
 *       ontop-toggle      pin/unpin the focused window "always on top"
 *                         (reflected back as "ontop" under "focused")
  *       sever <a> <b>     cut the connection between clients <a> and <b>
 *                         (see "connections" below)
 *       dockprep <appid> <x> <y> <w> <h>
 *                         arm a one-shot "dock this app_id straight into this
 *                         rect the moment it maps" request (see
 *                         dockprep_register()/dockprep_consume() in dwl.c).
 *                         Send this *before* spawning a panel's backing
 *                         terminal (its app_id won't exist as a real client
 *                         yet, so "dock" itself would no-op) so the very
 *                         first frame the client ever shows is already
 *                         docked — no flash at some default floating
 *                         position, no camera jump chasing it there. Consumed
 *                         on the next map of a client with that app_id; if no
 *                         such client ever maps it just sits harmlessly until
 *                         overwritten or the compositor exits.
 *       dock <appid> <x> <y> <w> <h>
 *                         pin the client with this app_id into an exact
 *                         screen-pixel rect: borderless, glued to that screen
 *                         position regardless of camera pan/zoom (see
 *                         setdocked()). For a panel embedding a real terminal
 *                         (e.g. a clipboard-history picker) at a fixed spot
 *                         in its own layout — spawn the client with a
 *                         recognizable app_id (after "dockprep", for the
 *                         first spawn), then re-issue "dock" any time the
 *                         panel's on-screen geometry changes, including every
 *                         later reopen of the same already-running client.
 *       undock <appid>    release a docked client back to a normal floating
 *                         window at its pre-dock geometry (does not hide or
 *                         kill it — pair with a minimize if the panel is
 *                         meant to fully disappear when closed)
 *       minimize <appid> <0|1>
 *                         hide/show a client by app_id without touching its
 *                         surface (see setminimized()) — pairs with
 *                         dock/undock so a docked panel can fully disappear
 *                         on close and pop back already-running on reopen,
 *                         addressed by app_id since the shell doesn't track
 *                         a numeric client id for a panel it just spawned
 *       set-output <name> <w> <h> <refresh> <scale> <x> <y> <enabled>
 *                         reconfigure a monitor by output name (see the
 *                         "outputs" state field above for names/current
 *                         values) — the IPC equivalent of what an external
 *                         wlr-output-management-v1 client like wlr-randr can
 *                         already do, addressed by name instead of that
 *                         protocol's own client-side config-head dance (see
 *                         ipc_set_output() in dwl.c, which shares the same
 *                         underlying wlr_output_state/commit path as
 *                         outputmgrapplyortest()). <w>/<h> <= 0 leaves the
 *                         mode unchanged; <refresh> <= 0 matches any refresh
 *                         rate at that resolution when picking a mode;
 *                         <scale> <= 0 leaves the scale unchanged — a caller
 *                         that only wants to reposition or disable an output
 *                         doesn't need to already know its current mode/scale
 *                         just to pass them through untouched.
 *       set-brightness <value>
 *                         set backlight brightness to an absolute raw value
 *                         (0..max from the "brightness" state field above) —
 *                         see backlight.c for why this goes through
 *                         logind's SetBrightness D-Bus method rather than a
 *                         direct sysfs write.
 *
 * The socket path is exported via $KALIN_IPC_SOCKET. Separately-compiled TU:
 * links against dwl.c's externed globals/functions (event_loop, selmon,
 * focustop, viewport_pan/zoom/reset/toggle_follow) and the shared viewport /
 * crop_editor state via kalin.h; client_* accessors from client_inline.h. */

#include "kalin.h"
#include "client_inline.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>

#define IPC_MAX_CLIENTS 16
/* Bumped from 4096 to fit the "outputs" array (each output's full mode
 * list) alongside everything else already in the state broadcast — see
 * ipc_build_state()'s outputs loop. */
#define IPC_BUF_SIZE    16384 /* raised from 8192: two monitors' full mode lists overflowed outputs[] */

struct ipc_client {
	int fd;
	struct wl_event_source *source;
	int resync; /* short write left a truncated record; lead the next send with '\n' */
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
	Client *c;
	Client *pending = connect_pick_pending();
	Monitor *m;
	struct wlr_output_mode *mode;
	char title[512];
	char appid[256];
	char conns[2048];
	char pendbuf[160];
	char dockhoverbuf[288];
	char outputs[4096];
	char cams[1024];
	size_t cams_len = 0;
	int cams_first = 1;
	char brightnessbuf[64];
	size_t conns_len = 0;
	int conns_first = 1;
	size_t outputs_len = 0;
	int outputs_first = 1;
	int bl_value, bl_max;
	int written;
	/* Focused window's on-screen rect (world -> screen through its holder's
	 * camera, matches resize()), so the shell can flow the radial buttons out
	 * of the actual window. Multi-camera: each client transforms through
	 * c->mon's camera; the coordinates are layout-global (include the
	 * monitor's offset), same space the connection rects below use. */
	int rx = f ? WORLD_TO_SCREEN_X(f->mon, f->geom.x) : 0;
	int ry = f ? WORLD_TO_SCREEN_Y(f->mon, f->geom.y) : 0;
	int rw = f ? (int)(f->geom.width  * MON_ZOOM_SAFE(f->mon)) : 0;
	int rh = f ? (int)(f->geom.height * MON_ZOOM_SAFE(f->mon)) : 0;

	ipc_json_escape(f ? client_get_title(f) : "", title, sizeof(title));
	ipc_json_escape(f ? client_get_appid(f) : "", appid, sizeof(appid));

	/* Connection graph (up to 8 directional neighbor slots per window), in
	 * the same world->screen rect shape as "rect" above. Always included
	 * (not gated on super_held here) so the shell doesn't need a second
	 * round-trip to learn the graph shape — it gates line *visibility* on
	 * its own super_held-equivalent signal. Compositor draws nothing;
	 * quickshell renders the lines and handles clicks on them, telling us
	 * which edge to cut via "sever <a_id> <b_id>". Each edge lives on both
	 * endpoints' neighbor[] arrays; only emit it from the lower-id side so
	 * it isn't broadcast twice. */
	conns[0] = '\0';
	wl_list_for_each(c, &clients, link) {
		int i;
		for (i = 0; i < 8; i++) {
			Client *nb = c->neighbor[i];
			int arx, ary, arw, arh, brx, bry, brw, brh, n;
			if (!nb || nb->id < c->id)
				continue;
			arx = WORLD_TO_SCREEN_X(c->mon, c->geom.x);
			ary = WORLD_TO_SCREEN_Y(c->mon, c->geom.y);
			arw = (int)(c->geom.width * MON_ZOOM_SAFE(c->mon));
			arh = (int)(c->geom.height * MON_ZOOM_SAFE(c->mon));
			brx = WORLD_TO_SCREEN_X(nb->mon, nb->geom.x);
			bry = WORLD_TO_SCREEN_Y(nb->mon, nb->geom.y);
			brw = (int)(nb->geom.width * MON_ZOOM_SAFE(nb->mon));
			brh = (int)(nb->geom.height * MON_ZOOM_SAFE(nb->mon));
			n = snprintf(conns + conns_len, sizeof(conns) - conns_len,
				"%s{\"a\":%u,\"b\":%u,"
				"\"a_rect\":{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d},"
				"\"b_rect\":{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}}",
				conns_first ? "" : ",",
				c->id, nb->id,
				arx, ary, arw, arh, brx, bry, brw, brh);
			if (n < 0 || (size_t)n >= sizeof(conns) - conns_len) {
				/* out of room; drop remaining entries — and erase the
				 * partial entry snprintf already wrote past conns_len,
				 * which would otherwise be emitted by the final %s and
				 * corrupt the JSON (found live: a truncated mode object
				 * made every state line unparseable for the shell) */
				conns[conns_len] = '\0';
				goto conns_full;
			}
			conns_len += (size_t)n;
			conns_first = 0;
		}
	}
conns_full:

	/* Live line for a menu-armed pending connect (Super+L, see
	 * connect_pick_arm() / connection_graph.c): the source window's screen
	 * rect plus the cursor's current screen position, so ConnectionLines.qml
	 * can draw a rubber-band from one to the other while it's armed. null
	 * when nothing's pending, same convention "connections" doesn't need
	 * since it's always an array — this one genuinely has an absent state. */
	if (pending) {
		int prx = WORLD_TO_SCREEN_X(pending->mon, pending->geom.x);
		int pry = WORLD_TO_SCREEN_Y(pending->mon, pending->geom.y);
		int prw = (int)(pending->geom.width * MON_ZOOM_SAFE(pending->mon));
		int prh = (int)(pending->geom.height * MON_ZOOM_SAFE(pending->mon));
		snprintf(pendbuf, sizeof(pendbuf),
			"{\"rect\":{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d},"
			"\"cursor\":{\"x\":%d,\"y\":%d}}",
			prx, pry, prw, prh,
			(int)cursor->x, (int)cursor->y);
	} else {
		snprintf(pendbuf, sizeof(pendbuf), "null");
	}

	/* Which docked client (see setdocked()) the cursor is currently over, if
	 * any — lets a shell panel auto-hide when the cursor leaves a real,
	 * compositor-positioned terminal, which the shell has no other way to
	 * observe (see dock_hover_client's declaration in dwl.c). */
	if (dock_hover_client) {
		char dhappid[256];
		ipc_json_escape(client_get_appid(dock_hover_client), dhappid, sizeof(dhappid));
		snprintf(dockhoverbuf, sizeof(dockhoverbuf), "\"%s\"", dhappid);
	} else {
		snprintf(dockhoverbuf, sizeof(dockhoverbuf), "null");
	}

	/* Backlight brightness (see backlight.c) — null if no backlight device
	 * was found (e.g. a desktop with no built-in panel). */
	if (backlight_get(&bl_value, &bl_max))
		snprintf(brightnessbuf, sizeof(brightnessbuf),
				"{\"value\":%d,\"max\":%d}", bl_value, bl_max);
	else
		snprintf(brightnessbuf, sizeof(brightnessbuf), "null");

	/* Every connected output's current state + full mode list — lets a
	 * shell panel (e.g. the display-settings TUI) read available
	 * resolutions/refresh rates and current mode/scale/position without
	 * shelling out to wlr-randr. Position is m->m (the Monitor's effective
	 * layout geometry, same field outputmgrapplyortest()/updatemons() use),
	 * not wlr_output's own coordinates, which aren't layout-relative. */
	outputs[0] = '\0';
	wl_list_for_each(m, &mons, link) {
		char modesbuf[2048];
		size_t modes_len = 0;
		int modes_first = 1;
		char oname[128];
		int n;

		if (!m->wlr_output)
			continue;
		ipc_json_escape(m->wlr_output->name, oname, sizeof(oname));

		modesbuf[0] = '\0';
		wl_list_for_each(mode, &m->wlr_output->modes, link) {
			n = snprintf(modesbuf + modes_len, sizeof(modesbuf) - modes_len,
				"%s{\"width\":%d,\"height\":%d,\"refresh\":%.3f}",
				modes_first ? "" : ",",
				mode->width, mode->height, mode->refresh / 1000.0f);
			if (n < 0 || (size_t)n >= sizeof(modesbuf) - modes_len) {
				/* out of room; drop remaining modes — erase the partial
				 * entry or the final %s emits broken JSON (see conns) */
				modesbuf[modes_len] = '\0';
				break;
			}
			modes_len += (size_t)n;
			modes_first = 0;
		}

		n = snprintf(outputs + outputs_len, sizeof(outputs) - outputs_len,
			"%s{\"name\":\"%s\",\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,"
			"\"refresh\":%.3f,\"scale\":%.3f,\"enabled\":%s,\"modes\":[%s]}",
			outputs_first ? "" : ",",
			oname, m->m.x, m->m.y,
			m->wlr_output->width, m->wlr_output->height,
			m->wlr_output->refresh / 1000.0f, m->wlr_output->scale,
			m->wlr_output->enabled ? "true" : "false",
			modesbuf);
		if (n < 0 || (size_t)n >= sizeof(outputs) - outputs_len) {
			/* out of room; drop remaining outputs — erase the partial
			 * entry or the final %s emits broken JSON (see conns) */
			outputs[outputs_len] = '\0';
			break;
		}
		outputs_len += (size_t)n;
		outputs_first = 0;
	}

	/* Per-monitor cameras (multi-camera): one entry per output keyed by name.
	 * The scalar "viewport" below stays for back-compat (shell OSD) and holds
	 * the cursor monitor's camera — the currently "active" one. */
	cams[0] = '\0';
	wl_list_for_each(m, &mons, link) {
		char cname[128];
		int n;
		if (!m->wlr_output)
			continue;
		ipc_json_escape(m->wlr_output->name, cname, sizeof(cname));
		n = snprintf(cams + cams_len, sizeof(cams) - cams_len,
			"%s{\"output\":\"%s\",\"x\":%.0f,\"y\":%.0f,\"zoom\":%.3f,"
			"\"follow\":%s,\"follow_new\":%s}",
			cams_first ? "" : ",", cname,
			m->cam.x, m->cam.y, m->cam.zoom,
			m->cam.follow ? "true" : "false",
			m->cam.follow_new_windows ? "true" : "false");
		if (n < 0 || (size_t)n >= sizeof(cams) - cams_len) {
			cams[cams_len] = '\0';
			break;
		}
		cams_len += (size_t)n;
		cams_first = 0;
	}

	written = snprintf(buf, len,
		"{\"type\":\"state\","
		"\"viewport\":{\"x\":%.0f,\"y\":%.0f,\"zoom\":%.3f,"
		"\"follow\":%s,\"follow_new\":%s},"
		"\"viewports\":[%s],"
		"\"crop\":%s,"
		"\"super_held\":%s,"
		"\"overview\":%s,"
		"\"menu\":%s,"
		"\"exit_pending\":%s,"
		"\"rect\":{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d},"
		"\"focused\":{\"appid\":\"%s\",\"title\":\"%s\","
		"\"fullscreen\":%s,\"ontop\":%s,\"overlap\":%s},"
		"\"connections\":[%s],"
		"\"pending_connect\":%s,"
		"\"dock_hover\":%s,"
		"\"outputs\":[%s],"
		"\"brightness\":%s}\n",
		selmon ? selmon->cam.x : 0.0f,
		selmon ? selmon->cam.y : 0.0f,
		selmon ? selmon->cam.zoom : 1.0f,
		(selmon && selmon->cam.follow) ? "true" : "false",
		(selmon && selmon->cam.follow_new_windows) ? "true" : "false",
		cams,
		crop_editor.active ? "true" : "false",
		super_held ? "true" : "false",
		overview_is_active() ? "true" : "false",
		menu_shown ? "true" : "false",
		exit_pending ? "true" : "false",
		rx, ry, rw, rh,
		appid, title,
		(f && f->isfullscreen) ? "true" : "false",
		(f && f->isontop) ? "true" : "false",
		(f && f->allow_overlap) ? "true" : "false",
		conns, pendbuf, dockhoverbuf, outputs, brightnessbuf);
	if ((written < 0 || (size_t)written >= len) && len >= 2) {
		/* Truncation cut off the trailing '\n'; restore the frame
		 * terminator so one oversized state costs the reader one bad
		 * record instead of desyncing the whole line stream. */
		wlr_log(WLR_ERROR, "ipc: state exceeds IPC_BUF_SIZE, truncated");
		buf[len - 2] = '\n';
		buf[len - 1] = '\0';
	}
}

/* Write one newline-terminated record, preserving the stream's line framing
 * across short writes on the non-blocking fd: a partial write leaves a
 * truncated record on the wire, and without its terminating '\n' every later
 * state would be glued onto it — the client's line splitter then drops all
 * subsequent states, not just one (seen live as quickshell's "bad state
 * line" warnings and docked panels stuck open on a stale dock_hover). On a
 * short write, lead the next send with '\n' so the reader loses exactly one
 * record and resyncs. */
static void
ipc_client_send(struct ipc_client *cl, const char *msg)
{
	size_t len = strlen(msg);
	ssize_t n;

	if (cl->resync) {
		if (write(cl->fd, "\n", 1) != 1)
			return; /* still clogged; retry at the next broadcast */
		cl->resync = 0;
	}
	n = write(cl->fd, msg, len);
	if (n < 0) {
		if (errno != EAGAIN && errno != EINTR)
			ipc_client_remove(cl);
		return;
	}
	if ((size_t)n < len)
		cl->resync = 1;
}

void
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
		ipc_client_send(&ipc_clients[i], raw);
	}
}

static void
ipc_exec_command(char *line)
{
	char *save = NULL;
	char *cmd = strtok_r(line, " \t\r", &save);
	if (!cmd)
		return;

	if (strcmp(cmd, "warp") == 0) {
		/* Warp the pointer to an absolute layout-pixel position and update
		 * selmon/focus as if the cursor really moved there. A test/automation
		 * hook: headless and nested test runs (see the headless multi-output
		 * harness) have no real pointer, so this is how an agent picks which
		 * monitor's camera the subsequent selmon-based ops (pan/zoom/fit) act
		 * on — deterministically, without injecting synthetic input events. */
		char *sx = strtok_r(NULL, " \t\r", &save);
		char *sy = strtok_r(NULL, " \t\r", &save);
		if (sx && sy) {
			wlr_cursor_warp(cursor, NULL, atof(sx), atof(sy));
			/* Non-zero time so motionnotify() runs its selmon/focus update
			 * (the internal time==0 path only restores pointer focus and
			 * skips selmon). The motion deltas are zero — this just
			 * re-points selmon at the warped-to monitor, which is the whole
			 * point of warp: pick which camera selmon-based ops act on. */
			motionnotify(1, NULL, 0, 0, 0, 0);
		} else {
			wlr_log(WLR_DEBUG, "ipc: warp: missing <x> <y>");
		}
	} else if (strcmp(cmd, "pan") == 0) {
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
	} else if (strcmp(cmd, "ontop-toggle") == 0) {
		Arg a = {0};
		toggleontop(&a);
	} else if (strcmp(cmd, "sever") == 0) {
		char *sa = strtok_r(NULL, " \t\r", &save);
		char *sb = strtok_r(NULL, " \t\r", &save);
		uint32_t id_a = sa ? (uint32_t)strtoul(sa, NULL, 10) : 0;
		uint32_t id_b = sb ? (uint32_t)strtoul(sb, NULL, 10) : 0;
		sever_connection(id_a, id_b);
	} else if (strcmp(cmd, "dockprep") == 0) {
		char *appid = strtok_r(NULL, " \t\r", &save);
		char *sx = strtok_r(NULL, " \t\r", &save);
		char *sy = strtok_r(NULL, " \t\r", &save);
		char *sw = strtok_r(NULL, " \t\r", &save);
		char *sh = strtok_r(NULL, " \t\r", &save);
		if (appid && sx && sy && sw && sh) {
			struct wlr_box rect = {
				.x = atoi(sx), .y = atoi(sy),
				.width = atoi(sw), .height = atoi(sh),
			};
			dockprep_register(appid, rect);
		} else {
			wlr_log(WLR_DEBUG, "ipc: dockprep: missing appid or args ('%s')",
					appid ? appid : "(none)");
		}
	} else if (strcmp(cmd, "dock") == 0) {
		char *appid = strtok_r(NULL, " \t\r", &save);
		char *sx = strtok_r(NULL, " \t\r", &save);
		char *sy = strtok_r(NULL, " \t\r", &save);
		char *sw = strtok_r(NULL, " \t\r", &save);
		char *sh = strtok_r(NULL, " \t\r", &save);
		Client *c = appid ? client_find_by_appid(appid) : NULL;
		if (c && sx && sy && sw && sh) {
			struct wlr_box rect = {
				.x = atoi(sx), .y = atoi(sy),
				.width = atoi(sw), .height = atoi(sh),
			};
			setdocked(c, 1, rect);
		} else {
			wlr_log(WLR_DEBUG, "ipc: dock: missing client or args ('%s')",
					appid ? appid : "(none)");
		}
	} else if (strcmp(cmd, "undock") == 0) {
		char *appid = strtok_r(NULL, " \t\r", &save);
		Client *c = appid ? client_find_by_appid(appid) : NULL;
		struct wlr_box unused = {0};
		if (c)
			setdocked(c, 0, unused);
	} else if (strcmp(cmd, "set-output") == 0) {
		char *name = strtok_r(NULL, " \t\r", &save);
		char *sw = strtok_r(NULL, " \t\r", &save);
		char *sh = strtok_r(NULL, " \t\r", &save);
		char *sr = strtok_r(NULL, " \t\r", &save);
		char *sscale = strtok_r(NULL, " \t\r", &save);
		char *sx = strtok_r(NULL, " \t\r", &save);
		char *sy = strtok_r(NULL, " \t\r", &save);
		char *senabled = strtok_r(NULL, " \t\r", &save);
		if (name && sw && sh && sr && sscale && sx && sy && senabled) {
			if (!ipc_set_output(name, atoi(sw), atoi(sh), (float)atof(sr),
					(float)atof(sscale), atoi(sx), atoi(sy), atoi(senabled) != 0))
				wlr_log(WLR_DEBUG, "ipc: set-output: unknown output or commit failed ('%s')",
						name);
		} else {
			wlr_log(WLR_DEBUG, "ipc: set-output: missing args ('%s')",
					name ? name : "(none)");
		}
	} else if (strcmp(cmd, "screenshot-ui") == 0) {
		/* Open the interactive screenshot UI (same as the Super+Shift+S
		 * bind) — lets a shell widget or script trigger it. */
		screenshotui_begin(NULL);
	} else if (strcmp(cmd, "screenshot") == 0) {
		/* Immediate whole-monitor capture (same as the Super+Print bind);
		 * lands in $KALIN_SHOT_DIR (or $HOME). */
		capture_screenshot(NULL);
	} else if (strcmp(cmd, "set-brightness") == 0) {
		char *sv = strtok_r(NULL, " \t\r", &save);
		if (sv) {
			if (!backlight_set(atoi(sv)))
				wlr_log(WLR_DEBUG, "ipc: set-brightness: no backlight device or logind call failed");
		} else {
			wlr_log(WLR_DEBUG, "ipc: set-brightness: missing value");
		}
	} else if (strcmp(cmd, "minimize") == 0) {
		char *appid = strtok_r(NULL, " \t\r", &save);
		char *sflag = strtok_r(NULL, " \t\r", &save);
		Client *c = appid ? client_find_by_appid(appid) : NULL;
		if (c && sflag)
			setminimized(c, atoi(sflag) != 0);
		else
			wlr_log(WLR_DEBUG, "ipc: minimize: missing client or args ('%s')",
					appid ? appid : "(none)");
	} else if (strcmp(cmd, "spotlight") == 0) {
		/* Deliberately a no-op: the hold-Super menu no longer zooms the camera
		 * (it snapped to wrong positions). Kept so a not-yet-rebuilt shell that
		 * still sends "spotlight 1/0" can't re-trigger the buggy zoom. Ensure any
		 * previously-applied dim is cleared. */
		spotlight_exit();
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
	ipc_clients[i].resync = 0;
	ipc_clients[i].source = wl_event_loop_add_fd(event_loop, cfd,
			WL_EVENT_READABLE, ipc_client_readable, &ipc_clients[i]);

	/* Greet the new client with the current state. */
	ipc_build_state(initial, sizeof(initial));
	ipc_client_send(&ipc_clients[i], initial);
	return 0;
}

void
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

void
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
