/* Unit tests for rail-order + overlay-attachment persistence (layout Phase 6 —
 * code/src/persistence.c, obsidian/implementation/persistence.md).
 *
 * persistence.c pulls kalin.h -> wlroots and can't be linked standalone, so —
 * exactly as test_rail.c does for the rail linkage — this file mirrors the two
 * pieces worth testing verbatim against a minimal mock:
 *
 *   1. the flat-JSON round-trip of the new fields: write them the way
 *      save_client_cb() does, parse them back the way loaded_state_from_object()
 *      does (via the same json_escape/json_find_string/json_find_int helpers,
 *      copied here), and assert the values survive;
 *   2. the order-independent relink in persistence_register_client(): a rail
 *      predecessor / overlay host names its partner by (appid,title,instance),
 *      and either endpoint may register first — whichever registers second must
 *      complete the link. Both orders must reach the same graph.
 *
 * The geometry restore, the /proc cmdline capture, and the file I/O are left to
 * the runtime round-trip (they're mechanical and OS-coupled). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>

static int test_failures = 0;
static int total_failures = 0;
static int test_passed = 0;
static int test_total = 0;

#define TEST(name) void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running " #name "... "); \
    test_total++; \
    test_failures = 0; \
    test_##name(); \
    if (test_failures == 0) { printf("PASS\n"); test_passed++; } \
    else { printf("FAIL (%d failures)\n", test_failures); total_failures += test_failures; } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "\n    ASSERT FAIL: " #cond " at line %d\n", __LINE__); \
        test_failures++; \
    } \
} while(0)

/* ---- verbatim mirror of persistence.c's JSON helpers ------------------ */

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
        default: fputc(*p, out); break;
        }
    }
    fputc('"', out);
}

static const char *
json_find_string(const char *obj, const char *key, char *dst, size_t dstlen)
{
    char pattern[64];
    const char *p, *end;
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

/* ---- mock SavedClientState (just the Phase 6 fields we test) ----------- */

typedef struct {
    char appid[128];
    char title[128];
    int instance;
    char rail_prev_appid[128];
    char rail_prev_title[128];
    int rail_prev_instance;
    char overlay_host_appid[128];
    char overlay_host_title[128];
    int overlay_host_instance;
    int overlay_off_x;
    int overlay_off_y;
} Saved;

/* Write the new fields the way save_client_cb() does, given a client's own
 * identity and its (already-resolved) predecessor/host identities. */
static void
write_client(FILE *fp, const Saved *s)
{
    fputs("    {", fp);
    fputs("\"appid\":", fp); json_escape(fp, s->appid);
    fputs(",\"title\":", fp); json_escape(fp, s->title);
    fprintf(fp, ",\"instance\":%d", s->instance);
    fputs(",\"rail_prev_appid\":", fp); json_escape(fp, s->rail_prev_appid);
    fputs(",\"rail_prev_title\":", fp); json_escape(fp, s->rail_prev_title);
    fprintf(fp, ",\"rail_prev_instance\":%d", s->rail_prev_instance);
    fputs(",\"overlay_host_appid\":", fp); json_escape(fp, s->overlay_host_appid);
    fputs(",\"overlay_host_title\":", fp); json_escape(fp, s->overlay_host_title);
    fprintf(fp, ",\"overlay_host_instance\":%d", s->overlay_host_instance);
    fprintf(fp, ",\"overlay_off_x\":%d,\"overlay_off_y\":%d",
        s->overlay_off_x, s->overlay_off_y);
    fputs("}", fp);
}

static void
parse_client(const char *obj, Saved *s)
{
    memset(s, 0, sizeof(*s));
    json_find_string(obj, "appid", s->appid, sizeof(s->appid));
    json_find_string(obj, "title", s->title, sizeof(s->title));
    s->instance = json_find_int(obj, "instance", 0);
    json_find_string(obj, "rail_prev_appid", s->rail_prev_appid, sizeof(s->rail_prev_appid));
    json_find_string(obj, "rail_prev_title", s->rail_prev_title, sizeof(s->rail_prev_title));
    s->rail_prev_instance = json_find_int(obj, "rail_prev_instance", 0);
    json_find_string(obj, "overlay_host_appid", s->overlay_host_appid, sizeof(s->overlay_host_appid));
    json_find_string(obj, "overlay_host_title", s->overlay_host_title, sizeof(s->overlay_host_title));
    s->overlay_host_instance = json_find_int(obj, "overlay_host_instance", 0);
    s->overlay_off_x = json_find_int(obj, "overlay_off_x", 0);
    s->overlay_off_y = json_find_int(obj, "overlay_off_y", 0);
}

/* Round-trip one Saved through a temp file (the same write->parse path the real
 * save/load uses, minus the array framing). */
static void
roundtrip(const Saved *in, Saved *out)
{
    char buf[1024];
    FILE *fp = tmpfile();
    long n;
    assert(fp);
    write_client(fp, in);
    fflush(fp);
    rewind(fp);
    n = (long)fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n > 0 ? n : 0] = '\0';
    fclose(fp);
    /* loaded_state_from_object() is handed one NUL-delimited object; the array
     * parser strips the trailing '}', so mirror that. */
    { char *brace = strrchr(buf, '}'); if (brace) *brace = '\0'; }
    parse_client(buf, out);
}

/* ---- mock relink (verbatim shape of persistence.c's Phase 6 block) ---- */

typedef struct MockClient {
    const char *appid, *title;
    int instance;
    struct MockClient *rail_prev, *rail_next;
    struct MockClient *overlay_host;
    int overlay_off_x, overlay_off_y;
    int isfloating;
} Client;

static Client *rail_head;
static Saved loaded[8];
static int loaded_n;
static Client *registered[8];
static int registered_n;

static const char *
identity_key(const char *appid, const char *title)
{
    if (appid && appid[0]) return appid;
    return title ? title : "";
}

static Client *
find_by_key(const char *appid, const char *title, int instance)
{
    const char *key = identity_key(appid, title);
    int i;
    for (i = 0; i < registered_n; i++)
        if (registered[i]->instance == instance
                && strcmp(identity_key(registered[i]->appid, registered[i]->title), key) == 0)
            return registered[i];
    return NULL;
}

static void
rail_insert_after(Client *p, Client *c)
{
    if (!c) return;
    if (!p) {
        if (!rail_head) rail_head = c;
        else for (p = rail_head; p->rail_next; p = p->rail_next) ;
        if (!p) { c->rail_prev = c->rail_next = NULL; return; }
    } else if (!p->rail_prev && !p->rail_next && rail_head != p) {
        if (!rail_head) { rail_head = p; p->rail_prev = p->rail_next = NULL; }
        else for (p = rail_head; p->rail_next; p = p->rail_next) ;
    }
    c->rail_prev = p;
    c->rail_next = p->rail_next;
    if (p->rail_next) p->rail_next->rail_prev = c;
    p->rail_next = c;
}

static void
rail_remove(Client *c)
{
    Client *prev, *next;
    if (!c || (!c->rail_prev && !c->rail_next && rail_head != c)) return;
    prev = c->rail_prev; next = c->rail_next;
    if (prev) prev->rail_next = next;
    if (next) next->rail_prev = prev;
    if (rail_head == c) rail_head = next;
    c->rail_prev = c->rail_next = NULL;
}

/* The relink block from persistence_register_client(), verbatim in shape:
 * given the just-registered client c and its own saved state (or NULL), resolve
 * both directions. */
static void
register_relink(Client *c, const Saved *state)
{
    const char *self_key = identity_key(c->appid, c->title);
    int i;

    if (state && state->rail_prev_appid[0]) {
        Client *pred = find_by_key(state->rail_prev_appid, state->rail_prev_title,
            state->rail_prev_instance);
        if (pred && pred != c && !c->rail_prev && !c->rail_next && rail_head != c)
            rail_insert_after(pred, c);
    }
    if (state && state->overlay_host_appid[0] && !c->overlay_host) {
        Client *host = find_by_key(state->overlay_host_appid, state->overlay_host_title,
            state->overlay_host_instance);
        if (host && host != c) {
            rail_remove(c);
            c->overlay_host = host;
            c->overlay_off_x = state->overlay_off_x;
            c->overlay_off_y = state->overlay_off_y;
            c->isfloating = 1;
        }
    }
    for (i = 0; i < loaded_n; i++) {
        Saved *ss = &loaded[i];
        Client *succ;
        if (ss->rail_prev_appid[0] && ss->rail_prev_instance == c->instance
                && strcmp(identity_key(ss->rail_prev_appid, ss->rail_prev_title), self_key) == 0) {
            succ = find_by_key(ss->appid, ss->title, ss->instance);
            if (succ && succ != c && !succ->rail_prev && !succ->rail_next && rail_head != succ)
                rail_insert_after(c, succ);
        }
        if (ss->overlay_host_appid[0] && ss->overlay_host_instance == c->instance
                && strcmp(identity_key(ss->overlay_host_appid, ss->overlay_host_title), self_key) == 0) {
            succ = find_by_key(ss->appid, ss->title, ss->instance);
            if (succ && succ != c && !succ->overlay_host) {
                rail_remove(succ);
                succ->overlay_host = c;
                succ->overlay_off_x = ss->overlay_off_x;
                succ->overlay_off_y = ss->overlay_off_y;
                succ->isfloating = 1;
            }
        }
    }
}

/* Look up a loaded Saved for a client's own identity (its find_saved_state). */
static const Saved *
saved_for(Client *c)
{
    int i;
    for (i = 0; i < loaded_n; i++)
        if (loaded[i].instance == c->instance
                && strcmp(identity_key(loaded[i].appid, loaded[i].title),
                    identity_key(c->appid, c->title)) == 0)
            return &loaded[i];
    return NULL;
}

static void
do_register(Client *c)
{
    registered[registered_n++] = c;
    register_relink(c, saved_for(c));
}

static void
reset_world(void)
{
    rail_head = NULL;
    loaded_n = registered_n = 0;
    memset(loaded, 0, sizeof(loaded));
    memset(registered, 0, sizeof(registered));
}

static void
order(char *out, size_t n)
{
    Client *c;
    size_t off = 0;
    out[0] = '\0';
    for (c = rail_head; c; c = c->rail_next) {
        int w = snprintf(out + off, n - off, "%s%s", off ? "-" : "", c->appid);
        if (w > 0) off += (size_t)w;
    }
}

/* ---- tests ------------------------------------------------------------ */

TEST(roundtrip_rail_predecessor)
{
    Saved in, out;
    memset(&in, 0, sizeof(in));
    snprintf(in.appid, sizeof(in.appid), "foot");
    snprintf(in.title, sizeof(in.title), "shell");
    in.instance = 1;
    snprintf(in.rail_prev_appid, sizeof(in.rail_prev_appid), "firefox");
    snprintf(in.rail_prev_title, sizeof(in.rail_prev_title), "web");
    in.rail_prev_instance = 0;
    roundtrip(&in, &out);
    ASSERT(strcmp(out.rail_prev_appid, "firefox") == 0);
    ASSERT(strcmp(out.rail_prev_title, "web") == 0);
    ASSERT(out.rail_prev_instance == 0);
    ASSERT(strcmp(out.appid, "foot") == 0 && out.instance == 1);
}

TEST(roundtrip_overlay_host_and_offset)
{
    Saved in, out;
    memset(&in, 0, sizeof(in));
    snprintf(in.appid, sizeof(in.appid), "discord");
    snprintf(in.overlay_host_appid, sizeof(in.overlay_host_appid), "minecraft");
    snprintf(in.overlay_host_title, sizeof(in.overlay_host_title), "MC");
    in.overlay_host_instance = 2;
    in.overlay_off_x = 20;
    in.overlay_off_y = -15;
    roundtrip(&in, &out);
    ASSERT(strcmp(out.overlay_host_appid, "minecraft") == 0);
    ASSERT(strcmp(out.overlay_host_title, "MC") == 0);
    ASSERT(out.overlay_host_instance == 2);
    ASSERT(out.overlay_off_x == 20);
    ASSERT(out.overlay_off_y == -15);
}

TEST(roundtrip_offrail_empty_edges)
{
    /* A float / rail-head window: empty predecessor + empty host survive as
     * empty (i.e. "no edge"), never spuriously matching another entry. */
    Saved in, out;
    memset(&in, 0, sizeof(in));
    snprintf(in.appid, sizeof(in.appid), "lone");
    roundtrip(&in, &out);
    ASSERT(out.rail_prev_appid[0] == '\0');
    ASSERT(out.overlay_host_appid[0] == '\0');
}

TEST(relink_rail_predecessor_first)
{
    /* Save: A (head) then B (rail_prev = A). Register A THEN B. */
    Client a = {0}, b = {0};
    char buf[64];
    reset_world();
    a.appid = "A"; a.title = "A"; a.instance = 0;
    b.appid = "B"; b.title = "B"; b.instance = 0;
    /* loaded: A with no predecessor, B with rail_prev = A */
    snprintf(loaded[0].appid, sizeof(loaded[0].appid), "A");
    snprintf(loaded[0].title, sizeof(loaded[0].title), "A");
    snprintf(loaded[1].appid, sizeof(loaded[1].appid), "B");
    snprintf(loaded[1].title, sizeof(loaded[1].title), "B");
    snprintf(loaded[1].rail_prev_appid, sizeof(loaded[1].rail_prev_appid), "A");
    snprintf(loaded[1].rail_prev_title, sizeof(loaded[1].rail_prev_title), "A");
    loaded_n = 2;
    do_register(&a);
    do_register(&b);
    order(buf, sizeof(buf));
    ASSERT(strcmp(buf, "A-B") == 0);
    ASSERT(rail_head == &a);
}

TEST(relink_rail_successor_first)
{
    /* Same save, but register B (the successor) FIRST, then A — order-
     * independent: the second registration must still complete the link. */
    Client a = {0}, b = {0};
    char buf[64];
    reset_world();
    a.appid = "A"; a.title = "A"; a.instance = 0;
    b.appid = "B"; b.title = "B"; b.instance = 0;
    snprintf(loaded[0].appid, sizeof(loaded[0].appid), "A");
    snprintf(loaded[0].title, sizeof(loaded[0].title), "A");
    snprintf(loaded[1].appid, sizeof(loaded[1].appid), "B");
    snprintf(loaded[1].title, sizeof(loaded[1].title), "B");
    snprintf(loaded[1].rail_prev_appid, sizeof(loaded[1].rail_prev_appid), "A");
    snprintf(loaded[1].rail_prev_title, sizeof(loaded[1].rail_prev_title), "A");
    loaded_n = 2;
    do_register(&b);   /* successor maps first */
    do_register(&a);   /* predecessor maps second, completes the link */
    order(buf, sizeof(buf));
    ASSERT(strcmp(buf, "A-B") == 0);
    ASSERT(rail_head == &a);
}

TEST(relink_three_member_chain)
{
    /* A-B-C rebuilt regardless of registration order (C, A, B here). */
    Client a = {0}, b = {0}, c = {0};
    char buf[64];
    reset_world();
    a.appid = "A"; a.title = "A";
    b.appid = "B"; b.title = "B";
    c.appid = "C"; c.title = "C";
    snprintf(loaded[0].appid, sizeof(loaded[0].appid), "A");
    snprintf(loaded[0].title, sizeof(loaded[0].title), "A");
    snprintf(loaded[1].appid, sizeof(loaded[1].appid), "B");
    snprintf(loaded[1].title, sizeof(loaded[1].title), "B");
    snprintf(loaded[1].rail_prev_appid, sizeof(loaded[1].rail_prev_appid), "A");
    snprintf(loaded[1].rail_prev_title, sizeof(loaded[1].rail_prev_title), "A");
    snprintf(loaded[2].appid, sizeof(loaded[2].appid), "C");
    snprintf(loaded[2].title, sizeof(loaded[2].title), "C");
    snprintf(loaded[2].rail_prev_appid, sizeof(loaded[2].rail_prev_appid), "B");
    snprintf(loaded[2].rail_prev_title, sizeof(loaded[2].rail_prev_title), "B");
    loaded_n = 3;
    do_register(&c);
    do_register(&a);
    do_register(&b);
    order(buf, sizeof(buf));
    ASSERT(strcmp(buf, "A-B-C") == 0);
    ASSERT(rail_head == &a);
}

TEST(relink_overlay_host_first)
{
    /* Child D pinned to host H. Register host first, then child. */
    Client h = {0}, d = {0};
    reset_world();
    h.appid = "H"; h.title = "H";
    d.appid = "D"; d.title = "D";
    snprintf(loaded[0].appid, sizeof(loaded[0].appid), "H");
    snprintf(loaded[0].title, sizeof(loaded[0].title), "H");
    snprintf(loaded[1].appid, sizeof(loaded[1].appid), "D");
    snprintf(loaded[1].title, sizeof(loaded[1].title), "D");
    snprintf(loaded[1].overlay_host_appid, sizeof(loaded[1].overlay_host_appid), "H");
    snprintf(loaded[1].overlay_host_title, sizeof(loaded[1].overlay_host_title), "H");
    loaded[1].overlay_off_x = 30;
    loaded[1].overlay_off_y = 40;
    loaded_n = 2;
    do_register(&h);
    do_register(&d);
    ASSERT(d.overlay_host == &h);
    ASSERT(d.overlay_off_x == 30 && d.overlay_off_y == 40);
    ASSERT(d.isfloating == 1);
    ASSERT(d.rail_prev == NULL && d.rail_next == NULL); /* off-rail */
}

TEST(relink_overlay_child_first)
{
    /* Same, but child registers before host — order-independent. */
    Client h = {0}, d = {0};
    reset_world();
    h.appid = "H"; h.title = "H";
    d.appid = "D"; d.title = "D";
    snprintf(loaded[0].appid, sizeof(loaded[0].appid), "H");
    snprintf(loaded[0].title, sizeof(loaded[0].title), "H");
    snprintf(loaded[1].appid, sizeof(loaded[1].appid), "D");
    snprintf(loaded[1].title, sizeof(loaded[1].title), "D");
    snprintf(loaded[1].overlay_host_appid, sizeof(loaded[1].overlay_host_appid), "H");
    snprintf(loaded[1].overlay_host_title, sizeof(loaded[1].overlay_host_title), "H");
    loaded[1].overlay_off_x = 30;
    loaded[1].overlay_off_y = 40;
    loaded_n = 2;
    do_register(&d);   /* child first: host not yet registered, no attach yet */
    ASSERT(d.overlay_host == NULL);
    do_register(&h);   /* host second completes the attach */
    ASSERT(d.overlay_host == &h);
    ASSERT(d.overlay_off_x == 30 && d.overlay_off_y == 40);
    ASSERT(d.isfloating == 1);
}

TEST(relink_no_partner_leaves_free)
{
    /* Child names a host that never maps this run: it stays a free window,
     * never attaching to a stale/absent pointer. */
    Client d = {0};
    reset_world();
    d.appid = "D"; d.title = "D";
    snprintf(loaded[0].appid, sizeof(loaded[0].appid), "D");
    snprintf(loaded[0].title, sizeof(loaded[0].title), "D");
    snprintf(loaded[0].overlay_host_appid, sizeof(loaded[0].overlay_host_appid), "GONE");
    snprintf(loaded[0].overlay_host_title, sizeof(loaded[0].overlay_host_title), "GONE");
    loaded_n = 1;
    do_register(&d);
    ASSERT(d.overlay_host == NULL);
    ASSERT(d.isfloating == 0);
}

int
main(void)
{
    printf("=== Persistence (rail + overlay) Tests ===\n");
    RUN_TEST(roundtrip_rail_predecessor);
    RUN_TEST(roundtrip_overlay_host_and_offset);
    RUN_TEST(roundtrip_offrail_empty_edges);
    RUN_TEST(relink_rail_predecessor_first);
    RUN_TEST(relink_rail_successor_first);
    RUN_TEST(relink_three_member_chain);
    RUN_TEST(relink_overlay_host_first);
    RUN_TEST(relink_overlay_child_first);
    RUN_TEST(relink_no_partner_leaves_free);

    printf("\n===================================\n");
    printf("Results: %d passed, %d total\n", test_passed, test_total);
    printf("Total assertion failures: %d\n", total_failures);
    printf("===================================\n");
    return total_failures > 0 ? 1 : 0;
}
