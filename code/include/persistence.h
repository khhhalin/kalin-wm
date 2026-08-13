/*
 * persistence.h - Canvas state persistence schema and operations
 */

#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include <time.h>
#include <stdint.h>

#define STATE_VERSION 1
#define STATE_DIR "~/.local/share/kalin-wm"
#define STATE_FILE "canvas_state.json"

typedef struct {
	char appid[128];
	char title[128];
	/* Which same-appid+title window this was, in spawn order (0 = first
	 * spawned this run). appid+title alone isn't a unique key — two
	 * simultaneously open windows of the same app (e.g. two plain "foot"
	 * terminals) share it — so multi-instance apps are disambiguated by
	 * spawn order instead: this run's Nth spawn of a given appid+title is
	 * matched against last run's Nth spawn of the same pair. Not perfect
	 * (it assumes spawn order repeats), but far better than every instance
	 * colliding on one saved slot. */
	int instance;
	int width;
	int height;
	float geom_x;
	float geom_y;
	int geom_set;
	int crop_active;
	float crop_x;
	float crop_y;
	float crop_w;
	float crop_h;
	int crop_base_w;
	int crop_base_h;
	int crop_saved_base;
	int isfullscreen;
	int isontop;
	float opacity;              /* Per-window alpha (Client.opacity, 0.1..1.0);
	                               1.0 = opaque, the default for pre-opacity saves. */
	/* Shell command that relaunches this client, captured at map time from
	 * /proc/<pid>/cmdline (pid via wl_client_get_credentials()) — every
	 * spawn already runs inside the persistent "kalin-apps" tmux session,
	 * so the *compositor* never knows the command and has to recover it
	 * from the client itself. Empty = layout-only restore (no respawn):
	 * either capture failed, the command couldn't be represented (the flat
	 * JSON parser brace-scans, so '{' '}' '[' ']' anywhere in the string
	 * would corrupt parsing), or the client is shell-panel chrome that
	 * DockedPanel respawns itself. A foot-server client (cmdline
	 * "foot --server" — respawning that daemon recreates zero windows) is
	 * substituted with "foot -e kalin-term", which reattaches the tmux
	 * content-persistence layer instead. */
	char cmd[512];
	/* Rail order (layout Phase 6). Each rail member saves the identity key of
	 * its rail_prev, so the 1D order rebuilds order-independently on load: when
	 * both a client and its saved predecessor are registered this run, they are
	 * spliced (whichever registers second completes the link — the same
	 * order-independent reconnect the removed connection-graph edges used). An
	 * off-rail window (float / overlay: rail_prev == rail_next == NULL at save)
	 * saves an empty rail_prev appid, i.e. no edge; the earliest rail member
	 * (rail_head, no predecessor) likewise saves none and starts the head. */
	char rail_prev_appid[128];
	char rail_prev_title[128];
	int rail_prev_instance;
	/* Attached overlay (layout Phase 6). An overlay child (overlay_host != NULL)
	 * saves its host's identity key plus the world-space follow offset, restored
	 * the same order-independent way: when both child and host are registered,
	 * the child's overlay_host + offsets are set and it's taken off the rail
	 * (like overlay_pin()). A non-overlay saves an empty overlay_host appid. */
	char overlay_host_appid[128];
	char overlay_host_title[128];
	int overlay_host_instance;
	int overlay_off_x;
	int overlay_off_y;
} SavedClientState;

typedef struct {
	int version;
	time_t timestamp;
	int client_count;
	SavedClientState *clients;
} CanvasState;

typedef void (*PersistenceClientFn)(const SavedClientState *state, void *data);

/* Initialize persistence system */
void persistence_init(void);

/* Save current canvas state (client geometry + cameras) to disk */
int persistence_save(void);

/* Load canvas state from disk */
int persistence_load(CanvasState *out);

/* Register a freshly-mapped managed client with the persistence system:
 * assigns it a stable (appid,title,instance) identity for this run, applies
 * any matching saved geometry/size/crop/fullscreen/ontop state, and relinks
 * any saved rail order + overlay attachment to whichever partner (rail
 * predecessor / overlay host) has already been registered this run — the
 * order-independent reconnect the connection-graph edges used to do (layout
 * Phase 6). Returns 1 if a saved absolute position
 * (geom_x/geom_y) was applied, 0 otherwise — callers use this to decide
 * whether to skip their own spawn-placement fallback. Call exactly once per
 * managed client, right after c->mon/c->geom are set but before any
 * placement fallback runs. */
int persistence_register_client(void *client);

/* Best-effort session resurrection: replay every saved client's captured
 * launch command (SavedClientState.cmd, see above) as a new window in the
 * persistent "kalin-apps" tmux session, so a compositor restart brings the
 * apps themselves back — persistence_register_client() then re-places each
 * one as it maps. Call exactly once at startup, from run() right after the
 * kalin-apps session bootstrap (the tmux server must already exist and
 * carry this compositor's WAYLAND_DISPLAY, or respawned apps can't reach
 * us). Entries are replayed in ascending saved-instance order so that, if
 * apps map in launch order, this run's per-appid instance counters assign
 * the same numbers the save file uses (best-effort: apps that race each
 * other to map can still swap instances). Skips entries with no command,
 * shell-panel chrome, and duplicates; capped, and never fails startup — a
 * bad entry is skipped, not fatal. */
void persistence_respawn_saved(void);

/* Undo persistence_register_client()'s bookkeeping when a client is
 * destroyed, so a later save doesn't describe a stale pointer and a later
 * registration doesn't try to reconnect to it. */
void persistence_unregister_client(void *client);

/* Iterate live clients via callback when saving */
void persistence_for_each_client(PersistenceClientFn fn, void *data);

/* Look up the saved camera (pan + zoom) for an output by name, so createmon()
 * can restore a monitor to exactly the view it had at the last save instead of
 * resetting to origin. Returns 1 and fills the out-params on a match with a
 * valid (zoom > 0) entry, 0 otherwise (leaving them untouched). Lazy-loads the
 * save file on first use, like persistence_register_client(). */
int persistence_camera_for_output(const char *output, float *x, float *y, float *zoom);

/* Free loaded state */
void persistence_free(CanvasState *state);

/* Cleanup */
void persistence_cleanup(void);

#endif /* PERSISTENCE_H */
