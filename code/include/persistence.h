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
	int width;
	int height;
	float world_x;
	float world_y;
	int world_set;
	int crop_active;
	float crop_x;
	float crop_y;
	float crop_w;
	float crop_h;
	int crop_base_w;
	int crop_base_h;
	int crop_saved_base;
	uint32_t tags;
	int isfloating;
	int isfullscreen;
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

/* Save current canvas state to disk */
int persistence_save(void);

/* Load canvas state from disk */
int persistence_load(CanvasState *out);

/* Match a saved state to a client identity */
const SavedClientState *persistence_find_match(const char *appid, const char *title);

/* Apply a loaded state to a client */
void persistence_apply_client(void *client);

/* Iterate live clients via callback when saving */
void persistence_for_each_client(PersistenceClientFn fn, void *data);

/* Free loaded state */
void persistence_free(CanvasState *state);

/* Cleanup */
void persistence_cleanup(void);

#endif /* PERSISTENCE_H */
