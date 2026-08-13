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
 *     "clients" is every mapped, non-panel toplevel:
 *     [{"id":<n>,"appid":"...","title":"...","focused":bool,
 *     "minimized":bool},...] — the taskbar feed for a shell that cannot
 *     speak foreign-toplevel-management (the kitty-hosted bar TUI reads
 *     this; QML shells should keep using the protocol). See "focus" below
 *     for acting on an entry.
 *   - Client -> server: plain text commands, one per line:
 *       pan <dx> <dy>     move the camera (world units)
 *       zoom <factor>     multiply zoom (e.g. 1.1 / 0.9)
 *       zoom-reset        reset camera to origin / 1.0
 *       follow-toggle     toggle camera-follows-focus
 *       ontop-toggle      pin/unpin the focused window "always on top"
 *                         (reflected back as "ontop" under "focused")
 *       focus <id>        focus the client with this stable id (from
 *                         "clients"), unminimizing it and centering the
 *                         camera on it — the taskbar-click action on an
 *                         infinite canvas, where "focus" alone could land
 *                         on a window nowhere near the current view.
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
 *       click [x y] [btn] synthetic pointer click via wlr_seat_* (agent input
 *                         hook — see the security note at ipc_synth_keysym).
 *                         With x y (layout pixels, same as warp) it warps there
 *                         first so pointer-enter lands on that surface; without,
 *                         it clicks the current cursor position. btn is
 *                         left/right/middle or a raw evdev code (default
 *                         BTN_LEFT). Replies {"type":"click","ok":true}.
 *       key <keysym>      synthesize one keypress on the CURRENTLY
 *                         keyboard-focused window (does NOT auto-focus — send
 *                         `focus <id>` first). <keysym> is an xkb keysym name
 *                         (Return, Escape, a) or 0x… hex. Replies one JSON line.
 *       type <utf8>       type a string into the keyboard-focused window, one
 *                         codepoint at a time (\n->Return, \t->Tab); the arg is
 *                         the rest of the line verbatim (spaces kept). Replies
 *                         {"type":"type","ok":true,"typed":N,"skipped":M} —
 *                         codepoints the keymap can't produce are skipped, never
 *                         mistyped. See ipc_keysym_to_evdev() for the reverse-map.
 *       screenshot-window <id:N|app-id:X> [WxH] [path]
 *                         capture ONE window to a PNG (capture_window()).
 *                         Renders the client's scene subtree in isolation, so it
 *                         works even if the window is occluded, off-screen, or
 *                         zoomed out; optional WxH scales the shot only (the live
 *                         window is untouched); path defaults to $KALIN_SHOT_DIR.
 *                         Unlike every other command this REPLIES, one JSON line:
 *                         {"type":"screenshot-window","ok":true,"path":..,
 *                         "width":W,"height":H}. `id` (from "clients") is unique;
 *                         app-id matches the first client with that app-id.
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
#include <time.h>

#define IPC_MAX_CLIENTS 16
/* Bumped from 4096 to fit the "outputs" array (each output's full mode
 * list) alongside everything else already in the state broadcast — see
 * ipc_build_state()'s outputs loop. */
#define IPC_BUF_SIZE    24576 /* raised again from 16384: the "clients" taskbar
                               * array (up to 4096B) joined the state line */

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
	Monitor *m;
	struct wlr_output_mode *mode;
	char title[512];
	char appid[256];
	char clientsbuf[4096];
	size_t clients_len = 0;
	int clients_first = 1;
	char dockhoverbuf[288];
	char outputs[4096];
	char cams[1024];
	size_t cams_len = 0;
	int cams_first = 1;
	char brightnessbuf[64];
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

	/* Taskbar feed: every mapped, non-panel toplevel. Truncation discipline:
	 * a partial entry must be erased or the final %s emits broken JSON.
	 * Panels (ispanel) are shell
	 * chrome and never taskbar entries, matching their exclusion from
	 * foreign-toplevel. */
	clientsbuf[0] = '\0';
	wl_list_for_each(c, &clients, link) {
		char cappid[256], ctitle[256];
		int n;
		if (!c->mon || !client_surface(c)->mapped || c->ispanel)
			continue;
		ipc_json_escape(client_get_appid(c), cappid, sizeof(cappid));
		ipc_json_escape(client_get_title(c), ctitle, sizeof(ctitle));
		n = snprintf(clientsbuf + clients_len, sizeof(clientsbuf) - clients_len,
			"%s{\"id\":%u,\"appid\":\"%s\",\"title\":\"%s\","
			"\"focused\":%s,\"minimized\":%s}",
			clients_first ? "" : ",",
			c->id, cappid, ctitle,
			c == f ? "true" : "false",
			c->minimized ? "true" : "false");
		if (n < 0 || (size_t)n >= sizeof(clientsbuf) - clients_len) {
			clientsbuf[clients_len] = '\0';
			goto clients_full;
		}
		clients_len += (size_t)n;
		clients_first = 0;
	}
clients_full:

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
				 * entry or the final %s emits broken JSON (see the clients loop) */
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
			 * entry or the final %s emits broken JSON (see the clients loop) */
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
		"\"fullscreen\":%s,\"ontop\":%s,\"overlap\":%s,\"yellow\":%.3f},"
		"\"clients\":[%s],"
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
		f ? f->paper_yellow : 0.0f,
		clientsbuf, dockhoverbuf, outputs, brightnessbuf);
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
static ssize_t ipc_client_drain(struct ipc_client *cl);

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
		if (errno != EAGAIN && errno != EINTR) {
			/* A fire-and-forget sender (kalin-dock, the bar TUI's panel
			 * toggles) may have already close()d: this write fails EPIPE,
			 * but its command still sits readable in the socket (half-close
			 * keeps data queued past the FIN). Removing without draining
			 * silently dropped the command — the accept-time greeting hit
			 * this on every fast one-shot client. Found live: the TUI bar's
			 * dockprep never registered and its panels mapped floating. */
			ipc_client_drain(cl);
			ipc_client_remove(cl);
		}
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

/* ---- Agent input synthesis (click / key / type) -------------------------
 *
 * These three commands let a socket-holder drive the live seat with synthetic
 * pointer and keyboard input via wlr_seat_* — the same job wtype/ydotool do,
 * without pulling in an external virtual-input protocol. Like the "warp"
 * test/automation hook above they exist for agent-driven verification: a
 * headless/nested run has no real input device, so this is how an agent clicks
 * a UI element or types into a window it just spawned.
 *
 * SECURITY: any client that can open this socket gets full synthetic
 * pointer+keyboard input into ANY window (a keylogger's write side, roughly).
 * Acceptable here because kalin-wm is a personal single-user compositor and the
 * socket already lives in $XDG_RUNTIME_DIR (0700, user-only); do not widen its
 * permissions without reconsidering this. Same trust boundary the "warp" and
 * "spawn"-adjacent commands already assume. */

/* Monotonic-ms timestamp in the same units real event->time_msec uses, so
 * synthetic events sit on the same clock as hardware ones (see dwl.c). */
static uint32_t
ipc_now_ms(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

/* Reverse-map a keysym to the (evdev keycode, xkb level) that produces it under
 * the active keymap — the crux of key/type. xkb only maps keycode+level ->
 * keysym forward, so we scan every keycode/level (layout 0) for a hit. Level ->
 * needed modifier follows the common ISO layout: L0 none, L1 Shift, L2 AltGr
 * (ISO_Level3_Shift); higher levels are rare and left unhandled (reported as
 * "no mapping" so the caller skips rather than mis-types). Returns the evdev
 * keycode (xkb keycode - 8, since wlr_seat_keyboard_notify_key wants evdev; see
 * keyboard.c) via *evdev_out and the level via *level_out; 0 on no mapping. */
static int
ipc_keysym_to_evdev(struct xkb_keymap *keymap, xkb_keysym_t target,
		uint32_t *evdev_out, xkb_level_index_t *level_out)
{
	xkb_keycode_t min = xkb_keymap_min_keycode(keymap);
	xkb_keycode_t max = xkb_keymap_max_keycode(keymap);
	xkb_keycode_t kc;

	for (kc = min; kc <= max; kc++) {
		xkb_level_index_t nlevels =
				xkb_keymap_num_levels_for_key(keymap, kc, 0);
		xkb_level_index_t lvl;
		for (lvl = 0; lvl < nlevels; lvl++) {
			const xkb_keysym_t *syms;
			int n = xkb_keymap_key_get_syms_by_level(keymap, kc, 0, lvl, &syms);
			int i;
			for (i = 0; i < n; i++) {
				if (syms[i] != target)
					continue;
				if (kc < 8) /* can't be an evdev keycode; skip degenerate map */
					continue;
				*evdev_out = (uint32_t)kc - 8;
				*level_out = lvl;
				return 1;
			}
		}
	}
	return 0;
}

/* Synthesize a full press+release of one keysym on the keyboard-focused
 * surface, holding whatever modifier the keysym's level requires.
 *
 * A raw modifier *key* event alone does NOT change what glyph the client
 * derives — a Wayland client tracks the effective modifier mask from the seat's
 * separate `modifiers` event, not by re-running xkb over key presses. So we send
 * the modifier as a mask via wlr_seat_keyboard_notify_modifiers() bracketing the
 * key (a bare notify_key with the Shift keycode types the unshifted glyph — seen
 * live: `>` came out `.`). Returns 0 and logs for any keysym the active keymap
 * can't produce (so type() skips it rather than emitting the wrong glyph). */
static int
ipc_synth_keysym(xkb_keysym_t sym)
{
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat);
	struct wlr_keyboard_modifiers mods = {0}, nomods = {0};
	uint32_t evdev, t;
	xkb_level_index_t lvl;

	if (!kb || !kb->keymap) {
		wlr_log(WLR_DEBUG, "ipc: key/type: no keyboard/keymap on seat");
		return 0;
	}
	if (!ipc_keysym_to_evdev(kb->keymap, sym, &evdev, &lvl)) {
		wlr_log(WLR_DEBUG, "ipc: key/type: keymap cannot produce keysym 0x%x, skipping",
				sym);
		return 0;
	}
	/* L0 = unmodified, L1 = Shift, L2 = AltGr (ISO_Level3_Shift, i.e. Mod5).
	 * These are the depressed-modifier bits a client folds into its own xkb
	 * state; higher levels are rare and left unhandled (report + skip). */
	if (lvl == 1)
		mods.depressed = WLR_MODIFIER_SHIFT;
	else if (lvl == 2)
		mods.depressed = WLR_MODIFIER_MOD5;
	else if (lvl != 0) {
		wlr_log(WLR_DEBUG, "ipc: key/type: keysym 0x%x needs level %u (unhandled), skipping",
				sym, lvl);
		return 0;
	}

	wlr_seat_set_keyboard(seat, kb);
	t = ipc_now_ms();
	if (mods.depressed)
		wlr_seat_keyboard_notify_modifiers(seat, &mods);
	wlr_seat_keyboard_notify_key(seat, t, evdev, WL_KEYBOARD_KEY_STATE_PRESSED);
	wlr_seat_keyboard_notify_key(seat, t, evdev, WL_KEYBOARD_KEY_STATE_RELEASED);
	if (mods.depressed)
		wlr_seat_keyboard_notify_modifiers(seat, &nomods);
	return 1;
}

static void
ipc_exec_command(struct ipc_client *cl, char *line)
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
	} else if (strcmp(cmd, "focus") == 0) {
		/* Taskbar click: focus by stable id (see "clients" above).
		 * Unminimize first — focusing a hidden client is a no-op the user
		 * reads as a dead button — and center the camera, since on an
		 * infinite canvas the window may be nowhere near the current view. */
		char *sid = strtok_r(NULL, " \t\r", &save);
		uint32_t id = sid ? (uint32_t)strtoul(sid, NULL, 10) : 0;
		Client *c = NULL, *it;
		wl_list_for_each(it, &clients, link) {
			if (it->id == id && it->mon && client_surface(it)->mapped
					&& !it->ispanel) {
				c = it;
				break;
			}
		}
		if (c) {
			if (c->minimized)
				setminimized(c, 0);
			focusclient(c, 1);
			viewport_center_on(c);
		} else {
			wlr_log(WLR_DEBUG, "ipc: focus: no client with id %u", id);
		}
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
	} else if (strcmp(cmd, "screenshot-window") == 0) {
		/* Capture ONE window to a PNG: `screenshot-window <id:N|app-id:X>
		 * [WxH] [path]`. Renders the client's scene subtree in isolation, so
		 * it works even if the window is occluded, off-screen, or zoomed out;
		 * optional WxH scales the shot only. Replies one JSON line so an
		 * agent gets the path + real dimensions without racing the FS. `id`
		 * is canonical (unique, from the "clients" feed); app-id hits the
		 * first match. */
		char *sel = strtok_r(NULL, " \t\r", &save);
		char *a2 = strtok_r(NULL, " \t\r", &save);
		char *a3 = strtok_r(NULL, " \t\r", &save);
		Client *c = NULL;
		int rw = 0, rh = 0, ow = 0, oh = 0;
		const char *path = NULL;
		char rpath[512], reply[640];

		if (sel && strncmp(sel, "id:", 3) == 0) {
			uint32_t id = (uint32_t)strtoul(sel + 3, NULL, 10);
			Client *it;
			wl_list_for_each(it, &clients, link)
				if (it->id == id) { c = it; break; }
		} else if (sel && strncmp(sel, "app-id:", 7) == 0) {
			c = client_find_by_appid(sel + 7);
		}
		/* a2 is WxH or a path; a3 (if present) is the path after a WxH. */
		if (a2) {
			if (sscanf(a2, "%dx%d", &rw, &rh) == 2)
				path = a3;
			else { rw = rh = 0; path = a2; }
		}
		if (c && capture_window(c, rw, rh, path, rpath, sizeof(rpath), &ow, &oh))
			snprintf(reply, sizeof(reply),
					"{\"type\":\"screenshot-window\",\"ok\":true,\"path\":\"%s\",\"width\":%d,\"height\":%d}\n",
					rpath, ow, oh);
		else
			snprintf(reply, sizeof(reply),
					"{\"type\":\"screenshot-window\",\"ok\":false,\"selector\":\"%s\"}\n",
					sel ? sel : "");
		ipc_client_send(cl, reply);
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
	} else if (strcmp(cmd, "click") == 0) {
		/* click [x y] [btn] — synthetic pointer click. With x y, warp there
		 * first (reusing warp's path) so pointer-enter lands on that surface;
		 * without, click wherever the cursor already is. btn: left/right/middle
		 * or a raw evdev button code; default BTN_LEFT. Replies one JSON line.
		 * See the security note on ipc_synth_keysym above. */
		char *a1 = strtok_r(NULL, " \t\r", &save);
		char *a2 = strtok_r(NULL, " \t\r", &save);
		char *a3 = strtok_r(NULL, " \t\r", &save);
		char *btnarg = NULL;
		uint32_t button = BTN_LEFT, t;
		char reply[64];

		/* If the first two tokens parse as numbers, treat them as x y and take
		 * the third (if any) as the button; otherwise the first token is the
		 * button and there is no warp. */
		if (a1 && a2) {
			char *ex = NULL, *ey = NULL;
			double x = strtod(a1, &ex), y = strtod(a2, &ey);
			if (ex != a1 && *ex == '\0' && ey != a2 && *ey == '\0') {
				wlr_cursor_warp(cursor, NULL, x, y);
				/* Non-zero time: run motionnotify's focus/enter update so
				 * pointer focus lands on the surface under (x,y) — same as warp. */
				motionnotify(1, NULL, 0, 0, 0, 0);
				btnarg = a3;
			} else {
				btnarg = a1;
			}
		} else if (a1) {
			btnarg = a1;
		}
		if (btnarg) {
			if (strcmp(btnarg, "left") == 0) button = BTN_LEFT;
			else if (strcmp(btnarg, "right") == 0) button = BTN_RIGHT;
			else if (strcmp(btnarg, "middle") == 0) button = BTN_MIDDLE;
			else button = (uint32_t)strtoul(btnarg, NULL, 0);
		}
		t = ipc_now_ms();
		wlr_seat_pointer_notify_button(seat, t, button, WL_POINTER_BUTTON_STATE_PRESSED);
		wlr_seat_pointer_notify_frame(seat);
		wlr_seat_pointer_notify_button(seat, t, button, WL_POINTER_BUTTON_STATE_RELEASED);
		wlr_seat_pointer_notify_frame(seat);
		snprintf(reply, sizeof(reply), "{\"type\":\"click\",\"ok\":true}\n");
		ipc_client_send(cl, reply);
	} else if (strcmp(cmd, "key") == 0) {
		/* key <keysym> — synthesize one keypress on the CURRENTLY
		 * keyboard-focused window (does NOT auto-focus: `focus <id>` first if
		 * needed). <keysym> is an xkb keysym name (Return, Escape, a) or 0x…
		 * hex. Replies one JSON line. */
		char *sk = strtok_r(NULL, " \t\r", &save);
		xkb_keysym_t sym = XKB_KEY_NoSymbol;
		char reply[80];

		if (sk) {
			if (strncmp(sk, "0x", 2) == 0 || strncmp(sk, "0X", 2) == 0)
				sym = (xkb_keysym_t)strtoul(sk, NULL, 16);
			else
				sym = xkb_keysym_from_name(sk, XKB_KEYSYM_NO_FLAGS);
		}
		if (sym == XKB_KEY_NoSymbol)
			snprintf(reply, sizeof(reply),
					"{\"type\":\"key\",\"ok\":false,\"err\":\"unknown keysym\"}\n");
		else if (ipc_synth_keysym(sym))
			snprintf(reply, sizeof(reply), "{\"type\":\"key\",\"ok\":true}\n");
		else
			snprintf(reply, sizeof(reply),
					"{\"type\":\"key\",\"ok\":false,\"err\":\"no keyboard or unmappable\"}\n");
		ipc_client_send(cl, reply);
	} else if (strcmp(cmd, "type") == 0) {
		/* type <utf8> — type a string into the keyboard-focused window, one
		 * codepoint at a time through the same reverse-map as `key`. Order
		 * preserved; \n -> Return, \t -> Tab. The argument is the rest of the
		 * line verbatim (spaces kept), so it is NOT strtok'd. Replies with the
		 * count typed vs skipped (unmappable codepoints are logged + skipped). */
		char *text = save; /* remainder of the line after "type " */
		int typed = 0, skipped = 0;
		char reply[96];

		if (text) {
			/* strtok_r left `save` pointing past the first delimiter run; a
			 * leading single space is the command separator and already
			 * consumed, but any further leading whitespace is real content. */
			while (*text) {
				/* Decode one UTF-8 codepoint (minimal, tolerant: a malformed
				 * byte is passed through as Latin-1 so we never desync). */
				unsigned char b0 = (unsigned char)text[0];
				uint32_t cp;
				int adv;
				xkb_keysym_t sym;
				if (b0 < 0x80) { cp = b0; adv = 1; }
				else if ((b0 & 0xE0) == 0xC0 && (text[1] & 0xC0) == 0x80) {
					cp = ((b0 & 0x1F) << 6) | (text[1] & 0x3F); adv = 2;
				} else if ((b0 & 0xF0) == 0xE0 && (text[1] & 0xC0) == 0x80
						&& (text[2] & 0xC0) == 0x80) {
					cp = ((b0 & 0x0F) << 12) | ((text[1] & 0x3F) << 6)
							| (text[2] & 0x3F); adv = 3;
				} else if ((b0 & 0xF8) == 0xF0 && (text[1] & 0xC0) == 0x80
						&& (text[2] & 0xC0) == 0x80 && (text[3] & 0xC0) == 0x80) {
					cp = ((b0 & 0x07) << 18) | ((text[1] & 0x3F) << 12)
							| ((text[2] & 0x3F) << 6) | (text[3] & 0x3F); adv = 4;
				} else { cp = b0; adv = 1; }
				text += adv;

				if (cp == '\n') sym = XKB_KEY_Return;
				else if (cp == '\t') sym = XKB_KEY_Tab;
				else sym = xkb_utf32_to_keysym(cp);

				if (sym != XKB_KEY_NoSymbol && ipc_synth_keysym(sym))
					typed++;
				else
					skipped++;
			}
		}
		snprintf(reply, sizeof(reply),
				"{\"type\":\"type\",\"ok\":true,\"typed\":%d,\"skipped\":%d}\n",
				typed, skipped);
		ipc_client_send(cl, reply);
	} else {
		wlr_log(WLR_DEBUG, "ipc: unknown command '%s'", cmd);
	}
}

/* Read and execute every command line the peer has queued. Returns the last
 * read() result: 0 = orderly EOF, -1 = would-block or error (the fd is
 * O_NONBLOCK, so this never stalls a live connection). Shared by the readable
 * handler and every disconnect path — a one-shot sender's command must be
 * executed even when its close() is noticed before (or instead of) its data. */
static ssize_t
ipc_client_drain(struct ipc_client *cl)
{
	char buf[IPC_BUF_SIZE];
	ssize_t n;

	for (;;) {
		char *save = NULL, *line;
		n = read(cl->fd, buf, sizeof(buf) - 1);
		if (n <= 0)
			return n;
		buf[n] = '\0';
		for (line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
			ipc_exec_command(cl, line);
	}
}

static int
ipc_client_readable(int fd, uint32_t mask, void *data)
{
	struct ipc_client *cl = data;
	ssize_t n;
	(void)fd;

	/* Drain BEFORE honoring a hangup: a fire-and-forget sender's HANGUP and
	 * READABLE arrive in the same wakeup, and removing the client without
	 * reading silently discarded its command (see ipc_client_send()). */
	n = ipc_client_drain(cl);

	if ((mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) || n == 0
			|| (n < 0 && errno != EAGAIN && errno != EINTR))
		ipc_client_remove(cl);
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
	size_t path_len;
	int i;

	for (i = 0; i < IPC_MAX_CLIENTS; i++)
		ipc_clients[i].fd = -1;

	if (!rundir)
		rundir = "/tmp";
	snprintf(ipc_socket_path, sizeof(ipc_socket_path), "%s/kalin-ipc-%s.sock",
			rundir, wl_display_name ? wl_display_name : "0");

	path_len = strlen(ipc_socket_path);
	if (path_len >= sizeof(addr.sun_path)) {
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
	/* Length was validated to fit above; copy the known-good length (plus
	 * the NUL) so this can't truncate — addr was zero-initialized. */
	memcpy(addr.sun_path, ipc_socket_path, path_len + 1);
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
