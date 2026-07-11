/* Remembers the last known good size for a window, keyed by app-id (falling
 * back to title), so relaunching the same app restores its previous size
 * instead of whatever default the client requests. A thin in-memory
 * lookup table — the actual persist-to-disk side lives in persistence.c;
 * this is purely the runtime key/lookup logic dwl.c's map/unmap paths call.
 *
 * Separately-compiled TU: links against dwl.c's client_get_appid()/
 * client_get_title() (via client_inline.h through kalin.h). Only
 * window_size_history_load()/store() are called from outside this file
 * (dwl.c); window_size_history_make_key() is a local helper for both. */
#include "kalin.h"
#include "client_inline.h"

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

static int
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
