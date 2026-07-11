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
} SavedClientState;

/* One saved connection-graph edge (Client.neighbor[]), identified by each
 * endpoint's (appid,title,instance) key rather than a runtime id — ids are
 * reassigned every run, so they can't survive a restart. */
typedef struct {
	char a_appid[128];
	char a_title[128];
	int a_instance;
	char b_appid[128];
	char b_title[128];
	int b_instance;
} SavedConnection;

typedef struct {
	int version;
	time_t timestamp;
	int client_count;
	SavedClientState *clients;
} CanvasState;

typedef void (*PersistenceClientFn)(const SavedClientState *state, void *data);

/* Initialize persistence system */
void persistence_init(void);

/* Save current canvas state (client geometry + connection graph) to disk */
int persistence_save(void);

/* Load canvas state from disk */
int persistence_load(CanvasState *out);

/* Register a freshly-mapped managed client with the persistence system:
 * assigns it a stable (appid,title,instance) identity for this run, applies
 * any matching saved geometry/size/crop/fullscreen/ontop state, and
 * reconnects any saved connection-graph edges to whichever partner has
 * already been registered this run. Returns 1 if a saved absolute position
 * (geom_x/geom_y) was applied, 0 otherwise — callers use this to decide
 * whether to skip their own spawn-placement fallback. Call exactly once per
 * managed client, right after c->mon/c->geom are set but before any
 * placement fallback runs. */
int persistence_register_client(void *client);

/* Undo persistence_register_client()'s bookkeeping when a client is
 * destroyed, so a later save doesn't describe a stale pointer and a later
 * registration doesn't try to reconnect to it. */
void persistence_unregister_client(void *client);

/* Iterate live clients via callback when saving */
void persistence_for_each_client(PersistenceClientFn fn, void *data);

/* Free loaded state */
void persistence_free(CanvasState *state);

/* Cleanup */
void persistence_cleanup(void);

#endif /* PERSISTENCE_H */
