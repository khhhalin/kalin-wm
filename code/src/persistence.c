/*
 * persistence.c - Canvas state persistence (JSON save/restore)
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../include/kalin.h"
#include "../include/client_inline.h"
#include "../include/persistence.h"

typedef struct LoadedStateNode {
	SavedClientState state;
	struct LoadedStateNode *next;
} LoadedStateNode;

static LoadedStateNode *loaded_states;
static int persistence_ready;

static char *
expand_home(const char *path)
{
	static char buf[512];
	const char *home;

	home = getenv("HOME");
	if (!home)
		home = "/root";
	if (path[0] == '~') {
		snprintf(buf, sizeof(buf), "%s%s", home, path + 1);
		return buf;
	}
	return (char *)path;
}

static const char *
persistence_path(void)
{
	static char pathbuf[512];
	snprintf(pathbuf, sizeof(pathbuf), "%s/%s", expand_home(STATE_DIR), STATE_FILE);
	return pathbuf;
}

static const char *
persistence_tmp_path(void)
{
	static char pathbuf[512];
	snprintf(pathbuf, sizeof(pathbuf), "%s/%s.tmp", expand_home(STATE_DIR), STATE_FILE);
	return pathbuf;
}

static void
loaded_state_free_all(void)
{
	LoadedStateNode *node;
	LoadedStateNode *next;

	for (node = loaded_states; node; node = next) {
		next = node->next;
		free(node);
	}
	loaded_states = NULL;
}

static LoadedStateNode *
loaded_state_push(void)
{
	LoadedStateNode *node = ecalloc(1, sizeof(*node));
	node->next = loaded_states;
	loaded_states = node;
	return node;
}

static void
json_escape(FILE *out, const char *text)
{
	const unsigned char *p;

	fputc('"', out);
	if (!text)
		text = "";
	for (p = (const unsigned char *)text; *p; p++) {
		switch (*p) {
		case '\\': fputs("\\\\", out); break;
		case '"': fputs("\\\"", out); break;
		case '\n': fputs("\\n", out); break;
		case '\r': fputs("\\r", out); break;
		case '\t': fputs("\\t", out); break;
		default:
			fputc(*p, out);
			break;
		}
	}
	fputc('"', out);
}

static const char *
json_find_string(const char *obj, const char *key, char *dst, size_t dstlen)
{
	char pattern[64];
	const char *p;
	const char *end;
	size_t i = 0;

	if (!obj || !key || !dst || dstlen == 0)
		return NULL;
	snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
	p = strstr(obj, pattern);
	if (!p)
		return NULL;
	p += strlen(pattern);
	end = p;
	while (*end && !(*end == '"' && *(end - 1) != '\\'))
		end++;
	for (; p < end && i + 1 < dstlen; p++) {
		if (*p == '\\' && (p + 1) < end) {
			p++;
			switch (*p) {
			case 'n': dst[i++] = '\n'; break;
			case 'r': dst[i++] = '\r'; break;
			case 't': dst[i++] = '\t'; break;
			default: dst[i++] = *p; break;
			}
		} else {
			dst[i++] = *p;
		}
	}
	dst[i] = '\0';
	return dst;
}

static int
json_find_int(const char *obj, const char *key, int fallback)
{
	char pattern[64];
	const char *p;
	int value;

	if (!obj || !key)
		return fallback;
	snprintf(pattern, sizeof(pattern), "\"%s\":", key);
	p = strstr(obj, pattern);
	if (!p)
		return fallback;
	p += strlen(pattern);
	if (sscanf(p, "%d", &value) != 1)
		return fallback;
	return value;
}

static float
json_find_float(const char *obj, const char *key, float fallback)
{
	char pattern[64];
	const char *p;
	float value;

	if (!obj || !key)
		return fallback;
	snprintf(pattern, sizeof(pattern), "\"%s\":", key);
	p = strstr(obj, pattern);
	if (!p)
		return fallback;
	p += strlen(pattern);
	if (sscanf(p, "%f", &value) != 1)
		return fallback;
	return value;
}

static void
loaded_state_from_object(const char *obj)
{
	LoadedStateNode *node;
	SavedClientState *state;

	node = loaded_state_push();
	state = &node->state;
	memset(state, 0, sizeof(*state));
	json_find_string(obj, "appid", state->appid, sizeof(state->appid));
	json_find_string(obj, "title", state->title, sizeof(state->title));
	state->width = json_find_int(obj, "width", 0);
	state->height = json_find_int(obj, "height", 0);
	state->world_x = json_find_float(obj, "world_x", 0.0f);
	state->world_y = json_find_float(obj, "world_y", 0.0f);
	state->world_set = json_find_int(obj, "world_set", 0);
	state->crop_active = json_find_int(obj, "crop_active", 0);
	state->crop_x = json_find_float(obj, "crop_x", 0.0f);
	state->crop_y = json_find_float(obj, "crop_y", 0.0f);
	state->crop_w = json_find_float(obj, "crop_w", 1.0f);
	state->crop_h = json_find_float(obj, "crop_h", 1.0f);
	state->crop_base_w = json_find_int(obj, "crop_base_w", 0);
	state->crop_base_h = json_find_int(obj, "crop_base_h", 0);
	state->crop_saved_base = json_find_int(obj, "crop_saved_base", 0);
	state->isfloating = json_find_int(obj, "isfloating", 0);
	state->isfullscreen = json_find_int(obj, "isfullscreen", 0);
}

static void
persistence_load_internal(void)
{
	FILE *fp;
	long len;
	char *buf;
	char *cur;
	char *object;
	char *end;

	loaded_state_free_all();
	fp = fopen(persistence_path(), "r");
	if (!fp)
		return;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return;
	}
	len = ftell(fp);
	if (len <= 0) {
		fclose(fp);
		return;
	}
	rewind(fp);
	buf = ecalloc((size_t)len + 1, 1);
	if (fread(buf, 1, (size_t)len, fp) != (size_t)len) {
		free(buf);
		fclose(fp);
		return;
	}
	fclose(fp);

	cur = strstr(buf, "\"clients\"");
	if (!cur) {
		free(buf);
		return;
	}
	cur = strchr(cur, '[');
	if (!cur) {
		free(buf);
		return;
	}
	for (;;) {
		object = strchr(cur, '{');
		if (!object)
			break;
		end = strchr(object, '}');
		if (!end)
			break;
		*end = '\0';
		loaded_state_from_object(object);
		cur = end + 1;
	}
	free(buf);
}

void
persistence_init(void)
{
	char *state_dir = expand_home(STATE_DIR);

	if (persistence_ready)
		return;
	persistence_ready = 1;
	mkdir(state_dir, 0755);
	persistence_load_internal();
}

const SavedClientState *
persistence_find_match(const char *appid, const char *title)
{
	LoadedStateNode *node;

	for (node = loaded_states; node; node = node->next) {
		if (node->state.appid[0] != '\0' && appid && strcmp(node->state.appid, appid) == 0)
			return &node->state;
		if (node->state.title[0] != '\0' && title && strcmp(node->state.title, title) == 0)
			return &node->state;
	}
	return NULL;
}

void
persistence_apply_client(void *client_ptr)
{
	Client *c = client_ptr;
	const SavedClientState *state;
	const char *appid;
	const char *title;

	if (!c)
		return;
	appid = client_get_appid(c);
	title = client_get_title(c);
	state = persistence_find_match(appid, title);
	if (!state)
		return;

	if (state->width > 0 && state->height > 0) {
		c->geom.width = MAX(1 + 2 * (int)c->bw, state->width);
		c->geom.height = MAX(1 + 2 * (int)c->bw, state->height);
	}
	if (state->world_set) {
		c->world.x = state->world_x;
		c->world.y = state->world_y;
		c->world.set = 1;
	}
	c->crop.active = state->crop_active;
	c->crop.x = state->crop_x;
	c->crop.y = state->crop_y;
	c->crop.w = state->crop_w;
	c->crop.h = state->crop_h;
	c->crop.base_w = state->crop_base_w;
	c->crop.base_h = state->crop_base_h;
	c->crop.saved_base = state->crop_saved_base;
	c->isfloating = state->isfloating;
	c->isfullscreen = state->isfullscreen;
}

void
persistence_for_each_client(PersistenceClientFn fn, void *data)
{
	Client *c;

	if (!fn)
		return;
	wl_list_for_each(c, &clients, link)
		fn((const SavedClientState *)c, data);
}

struct SaveCtx {
	FILE *fp;
	int first;
};

static void
save_client_cb(const SavedClientState *unused, void *data)
{
	struct SaveCtx *ctx = data;
	Client *c = (Client *)unused;
	const char *appid;
	const char *title;
	int save_w;
	int save_h;

	if (!ctx || !ctx->fp || !c)
		return;
	appid = client_get_appid(c);
	title = client_get_title(c);
	if (!ctx->first)
		fputs(",\n", ctx->fp);
	ctx->first = 0;

	save_w = c->geom.width;
	save_h = c->geom.height;
	if (c->crop.active && c->crop.saved_base && c->crop.base_w > 0 && c->crop.base_h > 0) {
		save_w = c->crop.base_w;
		save_h = c->crop.base_h;
	}

	fputs("    {", ctx->fp);
	fputs("\"appid\":", ctx->fp); json_escape(ctx->fp, appid);
	fputs(",\"title\":", ctx->fp); json_escape(ctx->fp, title);
	fprintf(ctx->fp, ",\"width\":%d,\"height\":%d", save_w, save_h);
	fprintf(ctx->fp, ",\"world_x\":%.2f,\"world_y\":%.2f,\"world_set\":%d",
		c->world.x, c->world.y, c->world.set);
	fprintf(ctx->fp, ",\"crop_active\":%d,\"crop_x\":%.4f,\"crop_y\":%.4f,\"crop_w\":%.4f,\"crop_h\":%.4f",
		c->crop.active, c->crop.x, c->crop.y, c->crop.w, c->crop.h);
	fprintf(ctx->fp, ",\"crop_base_w\":%d,\"crop_base_h\":%d,\"crop_saved_base\":%d",
		c->crop.base_w, c->crop.base_h, c->crop.saved_base);
	fprintf(ctx->fp, ",\"isfloating\":%d,\"isfullscreen\":%d}",
		c->isfloating, c->isfullscreen);
}

int
persistence_save(void)
{
	FILE *tmp;
	struct SaveCtx ctx = {.fp = NULL, .first = 1};

	if (!persistence_ready)
		persistence_init();
	tmp = fopen(persistence_tmp_path(), "w");
	if (!tmp)
		return -1;
	ctx.fp = tmp;
	fputs("{\n  \"version\":1,\n  \"timestamp\":", tmp);
	fprintf(tmp, "%ld", (long)time(NULL));
	fputs(",\n  \"clients\":[\n", tmp);
	persistence_for_each_client(save_client_cb, &ctx);
	fputs("\n  ]\n}\n", tmp);
	if (fflush(tmp) != 0 || fsync(fileno(tmp)) != 0) {
		fclose(tmp);
		unlink(persistence_tmp_path());
		return -1;
	}
	if (fclose(tmp) != 0) {
		unlink(persistence_tmp_path());
		return -1;
	}
	if (rename(persistence_tmp_path(), persistence_path()) != 0) {
		unlink(persistence_tmp_path());
		return -1;
	}
	return 0;
}

int
persistence_load(CanvasState *out)
{
	if (out) {
		out->version = STATE_VERSION;
		out->timestamp = time(NULL);
		out->client_count = 0;
		out->clients = NULL;
	}
	return 0;
}

void
persistence_free(CanvasState *state)
{
	if (!state)
		return;
	free(state->clients);
	state->clients = NULL;
	state->client_count = 0;
}

void
persistence_cleanup(void)
{
	loaded_state_free_all();
}
