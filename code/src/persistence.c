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

typedef struct LoadedConnNode {
	SavedConnection conn;
	struct LoadedConnNode *next;
} LoadedConnNode;

/* Every managed client that has called persistence_register_client() this
 * run, so save time can describe it (appid/title/instance) and so later
 * registrations can resolve a saved connection edge against it. */
typedef struct RegisteredClient {
	char appid[128];
	char title[128];
	int instance;
	void *client; /* Client*, opaque here */
	struct RegisteredClient *next;
} RegisteredClient;

/* How many times each (appid,title) pair has been registered this run so
 * far — the source of each new client's "instance" number. */
typedef struct InstanceCounter {
	char appid[128]; /* identity_key() result, despite the field name */
	int count;
	struct InstanceCounter *next;
} InstanceCounter;

static LoadedStateNode *loaded_states;
static LoadedConnNode *loaded_connections;
static RegisteredClient *registered_clients;
static InstanceCounter *instance_counters;
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
loaded_conn_free_all(void)
{
	LoadedConnNode *node, *next;

	for (node = loaded_connections; node; node = next) {
		next = node->next;
		free(node);
	}
	loaded_connections = NULL;
}

static LoadedConnNode *
loaded_conn_push(void)
{
	LoadedConnNode *node = ecalloc(1, sizeof(*node));
	node->next = loaded_connections;
	loaded_connections = node;
	return node;
}

static void
registered_clients_free_all(void)
{
	RegisteredClient *node, *next;

	for (node = registered_clients; node; node = next) {
		next = node->next;
		free(node);
	}
	registered_clients = NULL;
}

static void
instance_counters_free_all(void)
{
	InstanceCounter *node, *next;

	for (node = instance_counters; node; node = next) {
		next = node->next;
		free(node);
	}
	instance_counters = NULL;
}

/* The stable identity used for instance-counting/matching: appid whenever a
 * client has one (set at surface-creation time, protocol-guaranteed not to
 * change — reliable), falling back to title only for the rare client with no
 * appid at all. Title on its own is NOT safe to key on: many apps (e.g. a
 * terminal, whose window title starts as its own binary name before the
 * shell renames it a moment later) change their title shortly after
 * mapping, and depending on exact scheduling that rename can land before or
 * after persistence_register_client() runs — keying on title let the same
 * physical window key differently across two otherwise-identical runs,
 * silently splitting one counter into two and failing every match. */
static const char *
identity_key(const char *appid, const char *title)
{
	if (appid && appid[0])
		return appid;
	return title ? title : "";
}

/* Returns this identity's next unused instance number (0 on the first call
 * this run), creating its counter on first use. */
static int
next_instance_for(const char *appid, const char *title)
{
	const char *key = identity_key(appid, title);
	InstanceCounter *node;

	for (node = instance_counters; node; node = node->next) {
		if (strcmp(node->appid, key) == 0)
			return node->count++;
	}
	node = ecalloc(1, sizeof(*node));
	snprintf(node->appid, sizeof(node->appid), "%s", key);
	node->count = 1;
	node->next = instance_counters;
	instance_counters = node;
	return 0;
}

static RegisteredClient *
registered_find_by_key(const char *appid, const char *title, int instance)
{
	const char *key = identity_key(appid, title);
	RegisteredClient *r;

	for (r = registered_clients; r; r = r->next)
		if (r->instance == instance && strcmp(identity_key(r->appid, r->title), key) == 0)
			return r;
	return NULL;
}

static RegisteredClient *
registered_find_by_client(void *client_ptr)
{
	RegisteredClient *r;

	for (r = registered_clients; r; r = r->next)
		if (r->client == client_ptr)
			return r;
	return NULL;
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
	state->instance = json_find_int(obj, "instance", 0);
	state->width = json_find_int(obj, "width", 0);
	state->height = json_find_int(obj, "height", 0);
	state->geom_x = json_find_float(obj, "geom_x", 0.0f);
	state->geom_y = json_find_float(obj, "geom_y", 0.0f);
	state->geom_set = json_find_int(obj, "geom_set", 0);
	state->crop_active = json_find_int(obj, "crop_active", 0);
	state->crop_x = json_find_float(obj, "crop_x", 0.0f);
	state->crop_y = json_find_float(obj, "crop_y", 0.0f);
	state->crop_w = json_find_float(obj, "crop_w", 1.0f);
	state->crop_h = json_find_float(obj, "crop_h", 1.0f);
	state->crop_base_w = json_find_int(obj, "crop_base_w", 0);
	state->crop_base_h = json_find_int(obj, "crop_base_h", 0);
	state->crop_saved_base = json_find_int(obj, "crop_saved_base", 0);
	state->isfullscreen = json_find_int(obj, "isfullscreen", 0);
	state->isontop = json_find_int(obj, "isontop", 0);
}

static void
loaded_conn_from_object(const char *obj)
{
	LoadedConnNode *node = loaded_conn_push();
	SavedConnection *conn = &node->conn;

	memset(conn, 0, sizeof(*conn));
	json_find_string(obj, "a_appid", conn->a_appid, sizeof(conn->a_appid));
	json_find_string(obj, "a_title", conn->a_title, sizeof(conn->a_title));
	conn->a_instance = json_find_int(obj, "a_instance", 0);
	json_find_string(obj, "b_appid", conn->b_appid, sizeof(conn->b_appid));
	json_find_string(obj, "b_title", conn->b_title, sizeof(conn->b_title));
	conn->b_instance = json_find_int(obj, "b_instance", 0);
}

/* Matching ']' for the '[' at open_bracket, by depth-counting brackets —
 * safe here because none of our saved objects contain a nested array. */
static char *
find_matching_close_bracket(char *open_bracket)
{
	int depth = 0;
	char *p;

	for (p = open_bracket; *p; p++) {
		if (*p == '[')
			depth++;
		else if (*p == ']') {
			depth--;
			if (depth == 0)
				return p;
		}
	}
	return NULL;
}

/* Parse every flat {...} object inside the named top-level array, calling
 * per_object(text) for each — bounded by the array's own matching ']' so
 * this doesn't run on past it into whatever JSON array comes next (the
 * original version of this parser had no such bound and would have silently
 * also parsed "connections" objects as client states once that array was
 * added below it). */
static void
parse_json_array(char *buf, const char *key, void (*per_object)(const char *))
{
	char *cur, *arr_start, *arr_end, *object, *end;

	cur = strstr(buf, key);
	if (!cur)
		return;
	arr_start = strchr(cur, '[');
	if (!arr_start)
		return;
	arr_end = find_matching_close_bracket(arr_start);

	cur = arr_start;
	for (;;) {
		object = strchr(cur, '{');
		if (!object || (arr_end && object > arr_end))
			break;
		end = strchr(object, '}');
		if (!end || (arr_end && end > arr_end))
			break;
		*end = '\0';
		per_object(object);
		cur = end + 1;
	}
}

static void
persistence_load_internal(void)
{
	FILE *fp;
	long len;
	char *buf;

	loaded_state_free_all();
	loaded_conn_free_all();
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

	parse_json_array(buf, "\"clients\"", loaded_state_from_object);
	parse_json_array(buf, "\"connections\"", loaded_conn_from_object);
	free(buf);
}

/* mkdir -p: create every path component that's missing. Plain mkdir() on
 * STATE_DIR ("~/.local/share/kalin-wm") is non-recursive, and a minimal
 * system (this compositor's own test VM, for one) may not have
 * ~/.local/share yet — nothing else creates XDG dirs on its behalf. That
 * silently failed the mkdir, which silently failed every later fopen(),
 * which meant persistence never actually wrote or read anything, ever,
 * without a word about it. */
static void
mkdir_p(char *path)
{
	char *p;

	for (p = path + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		mkdir(path, 0755);
		*p = '/';
	}
	mkdir(path, 0755);
}

void
persistence_init(void)
{
	char *state_dir = expand_home(STATE_DIR);

	if (persistence_ready)
		return;
	persistence_ready = 1;
	mkdir_p(state_dir);
	persistence_load_internal();
}

/* identity_key()+instance match — see identity_key()'s comment for why
 * title can't be part of this. The old version matched on appid OR title
 * independently, which could match a save entry with the same title but a
 * completely different appid. */
static const SavedClientState *
find_saved_state(const char *appid, const char *title, int instance)
{
	const char *key = identity_key(appid, title);
	LoadedStateNode *node;

	for (node = loaded_states; node; node = node->next) {
		if (node->state.instance != instance)
			continue;
		if (strcmp(identity_key(node->state.appid, node->state.title), key) == 0)
			return &node->state;
	}
	return NULL;
}

int
persistence_register_client(void *client_ptr)
{
	Client *c = client_ptr;
	const char *appid;
	const char *title;
	int instance;
	RegisteredClient *reg;
	const SavedClientState *state;
	LoadedConnNode *ln;
	int applied_geom = 0;

	if (!c)
		return 0;
	if (!persistence_ready)
		persistence_init();

	appid = client_get_appid(c);
	title = client_get_title(c);
	instance = next_instance_for(appid, title);

	reg = ecalloc(1, sizeof(*reg));
	snprintf(reg->appid, sizeof(reg->appid), "%s", appid ? appid : "");
	snprintf(reg->title, sizeof(reg->title), "%s", title ? title : "");
	reg->instance = instance;
	reg->client = c;
	reg->next = registered_clients;
	registered_clients = reg;

	state = find_saved_state(appid, title, instance);
	if (state) {
		if (state->width > 0 && state->height > 0) {
			c->geom.width = MAX(1 + 2 * (int)c->bw, state->width);
			c->geom.height = MAX(1 + 2 * (int)c->bw, state->height);
			/* See the field's comment in kalin.h: this client hasn't
			 * finished its own first size-negotiation commit yet, and
			 * that commit would otherwise silently overwrite the
			 * restored size the instant it arrives. */
			c->persist_size_pending = 1;
		}
		if (state->geom_set) {
			c->geom.x = (int)state->geom_x;
			c->geom.y = (int)state->geom_y;
			applied_geom = 1;
		}
		c->crop.active = state->crop_active;
		c->crop.x = state->crop_x;
		c->crop.y = state->crop_y;
		c->crop.w = state->crop_w;
		c->crop.h = state->crop_h;
		c->crop.base_w = state->crop_base_w;
		c->crop.base_h = state->crop_base_h;
		c->crop.saved_base = state->crop_saved_base;
		c->isfullscreen = state->isfullscreen;
		c->isontop = state->isontop;
		/* Nothing else applies c->geom to the scene node for this path
		 * (the spawn-placement branches this replaces each call resize()
		 * themselves) — without this, a restored position/size would sit
		 * in c->geom but never actually move the window on screen. */
		resize(c, c->geom, 0);
	}

	/* Reconnect any saved edge naming this client, to whichever partner
	 * has already been registered this run (order-independent: whichever
	 * of the two maps second is the one that completes the edge).
	 * connect_clients() no-ops safely if a slot's already taken, so this
	 * is safe to attempt even if geometry doesn't perfectly agree with the
	 * octant it implies. */
	{
		const char *self_key = identity_key(reg->appid, reg->title);

		for (ln = loaded_connections; ln; ln = ln->next) {
			SavedConnection *conn = &ln->conn;
			RegisteredClient *other = NULL;

			if (conn->a_instance == instance
					&& strcmp(identity_key(conn->a_appid, conn->a_title), self_key) == 0)
				other = registered_find_by_key(conn->b_appid, conn->b_title, conn->b_instance);
			else if (conn->b_instance == instance
					&& strcmp(identity_key(conn->b_appid, conn->b_title), self_key) == 0)
				other = registered_find_by_key(conn->a_appid, conn->a_title, conn->a_instance);
			if (other)
				connect_clients(c, (Client *)other->client);
		}
	}

	return applied_geom;
}

void
persistence_unregister_client(void *client_ptr)
{
	RegisteredClient **pp = &registered_clients;

	while (*pp) {
		if ((*pp)->client == client_ptr) {
			RegisteredClient *dead = *pp;
			*pp = dead->next;
			free(dead);
			return;
		}
		pp = &(*pp)->next;
	}
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
	RegisteredClient *reg;
	int save_w;
	int save_h;

	if (!ctx || !ctx->fp || !c)
		return;
	/* Every managed client was registered at map time (see
	 * persistence_register_client(), called from mapnotify()) — this
	 * lookup only fails for a client type that skipped registration, in
	 * which case falling back to a fresh query is the best we can do.
	 * Deliberately use the REGISTERED (appid,title) snapshot, not a fresh
	 * client_get_appid()/client_get_title() query: many apps (e.g. a
	 * terminal before its shell renames the window) change their title
	 * shortly after mapping, and save_connections() below identifies
	 * clients by their registered snapshot too — querying fresh here
	 * would key the "clients" and "connections" arrays inconsistently,
	 * silently breaking connection restoration on the next load. */
	reg = registered_find_by_client(c);
	appid = reg ? reg->appid : client_get_appid(c);
	title = reg ? reg->title : client_get_title(c);
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
	fprintf(ctx->fp, ",\"instance\":%d", reg ? reg->instance : 0);
	fprintf(ctx->fp, ",\"width\":%d,\"height\":%d", save_w, save_h);
	fprintf(ctx->fp, ",\"geom_x\":%.2f,\"geom_y\":%.2f,\"geom_set\":1",
		(float)c->geom.x, (float)c->geom.y);
	fprintf(ctx->fp, ",\"crop_active\":%d,\"crop_x\":%.4f,\"crop_y\":%.4f,\"crop_w\":%.4f,\"crop_h\":%.4f",
		c->crop.active, c->crop.x, c->crop.y, c->crop.w, c->crop.h);
	fprintf(ctx->fp, ",\"crop_base_w\":%d,\"crop_base_h\":%d,\"crop_saved_base\":%d",
		c->crop.base_w, c->crop.base_h, c->crop.saved_base);
	fprintf(ctx->fp, ",\"isfullscreen\":%d,\"isontop\":%d}",
		c->isfullscreen, c->isontop);
}

/* Save every live connection-graph edge, identified by each endpoint's
 * (appid,title,instance) key rather than its runtime id (ids aren't stable
 * across restarts). Dedups the same way the IPC broadcast does: only emit
 * from the lower-id side, since a<->b and b<->a are the same undirected
 * edge. */
static void
save_connections(FILE *fp)
{
	Client *c;
	int first = 1;

	fputs(",\n  \"connections\":[\n", fp);
	wl_list_for_each(c, &clients, link) {
		RegisteredClient *ra = registered_find_by_client(c);
		int i;

		if (!ra)
			continue;
		for (i = 0; i < 8; i++) {
			Client *n = c->neighbor[i];
			RegisteredClient *rb;

			if (!n || n->id <= c->id)
				continue;
			rb = registered_find_by_client(n);
			if (!rb)
				continue;
			if (!first)
				fputs(",\n", fp);
			first = 0;
			fputs("    {", fp);
			fputs("\"a_appid\":", fp); json_escape(fp, ra->appid);
			fputs(",\"a_title\":", fp); json_escape(fp, ra->title);
			fprintf(fp, ",\"a_instance\":%d", ra->instance);
			fputs(",\"b_appid\":", fp); json_escape(fp, rb->appid);
			fputs(",\"b_title\":", fp); json_escape(fp, rb->title);
			fprintf(fp, ",\"b_instance\":%d}", rb->instance);
		}
	}
	fputs("\n  ]\n", fp);
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
	fputs("\n  ]", tmp);
	save_connections(tmp);
	fputs("}\n", tmp);
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
	loaded_conn_free_all();
	registered_clients_free_all();
	instance_counters_free_all();
}
