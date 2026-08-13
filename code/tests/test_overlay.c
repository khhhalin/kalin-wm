/* Unit tests for the attached-overlay follow math (layout Phase 5 — the
 * overlay_host/overlay_off follow hook in resize() and overlay_pin() in dwl.c,
 * obsidian/implementation/float-overlay.md). Like test_rail.c/test_float.c, the
 * real hook calls into wlroots (resize, client_set_target_geom) and dwl.c
 * globals that can't link standalone, so this reimplements the *pure geometry
 * and pointer discipline* worth testing against a minimal mock Client:
 *
 *  - a child's geom tracks its host at the fixed offset when the host moves;
 *  - one host drives several children, each at its own offset;
 *  - the pin refuses a self-pin and a chain (host is itself a child), which is
 *    what keeps the resize() re-entrancy guard's "hosts don't chain" invariant;
 *  - the follow walk visits only real children (off-target hosts untouched). */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

typedef struct MockClient {
    int id;
    int x, y, w, h;
    struct MockClient *overlay_host;
    int overlay_off_x, overlay_off_y;
} Client;

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

/* ---- mirror of overlay_pin()'s refusal rules (pure pointer logic) ------ */
static int
overlay_pin(Client *child, Client *host, int dx, int dy)
{
    if (!child || !host || child == host)
        return 0;
    if (host->overlay_host) /* no chaining */
        return 0;
    child->overlay_host = host;
    child->overlay_off_x = dx;
    child->overlay_off_y = dy;
    return 1;
}

/* Mirror of the resize() follow walk: after a host moves, every child pinned to
 * it is repositioned to host.origin + its offset (size unchanged). `all`/`n` is
 * the client list the walk scans. */
static void
overlay_follow(Client *host, Client **all, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        Client *child = all[i];
        if (child->overlay_host != host || child == host)
            continue;
        child->x = host->x + child->overlay_off_x;
        child->y = host->y + child->overlay_off_y;
    }
}

/* ---- tests ------------------------------------------------------------- */

TEST(child_tracks_host_move)
{
    Client host = {1, 100, 100, 800, 600, NULL, 0, 0};
    Client child = {2, 0, 0, 200, 150, NULL, 0, 0};
    Client *all[] = {&host, &child};
    ASSERT(overlay_pin(&child, &host, 20, 20) == 1);
    /* snap-in would place it; simulate the initial follow */
    overlay_follow(&host, all, 2);
    ASSERT(child.x == 120 && child.y == 120);
    /* host moves (rail-swap / drag): child tracks by the same delta */
    host.x = 500; host.y = 300;
    overlay_follow(&host, all, 2);
    ASSERT(child.x == 520 && child.y == 320);
    /* size never changes on follow */
    ASSERT(child.w == 200 && child.h == 150);
}

TEST(negative_offset_corner_pin)
{
    /* A corner overlay pinned above-left of the host (negative offset). */
    Client host = {1, 1000, 1000, 800, 600, NULL, 0, 0};
    Client child = {2, 0, 0, 100, 100, NULL, 0, 0};
    Client *all[] = {&host, &child};
    overlay_pin(&child, &host, -120, -120);
    overlay_follow(&host, all, 2);
    ASSERT(child.x == 880 && child.y == 880);
    host.x -= 200;
    overlay_follow(&host, all, 2);
    ASSERT(child.x == 680);
}

TEST(host_drives_multiple_children)
{
    Client host = {1, 0, 0, 800, 600, NULL, 0, 0};
    Client a = {2, 0, 0, 100, 100, NULL, 0, 0};
    Client b = {3, 0, 0, 100, 100, NULL, 0, 0};
    Client *all[] = {&host, &a, &b};
    overlay_pin(&a, &host, 10, 10);
    overlay_pin(&b, &host, 700, 500);
    host.x = 300; host.y = 200;
    overlay_follow(&host, all, 3);
    ASSERT(a.x == 310 && a.y == 210);
    ASSERT(b.x == 1000 && b.y == 700);
}

TEST(refuses_self_pin)
{
    Client host = {1, 0, 0, 800, 600, NULL, 0, 0};
    ASSERT(overlay_pin(&host, &host, 0, 0) == 0);
    ASSERT(host.overlay_host == NULL);
}

TEST(refuses_chain_pin)
{
    /* host is itself an overlay child -> pinning onto it is refused, keeping the
     * "hosts don't chain" invariant the resize() re-entrancy guard relies on. */
    Client grand = {1, 0, 0, 800, 600, NULL, 0, 0};
    Client host = {2, 0, 0, 400, 300, &grand, 0, 0};
    Client child = {3, 0, 0, 100, 100, NULL, 0, 0};
    ASSERT(overlay_pin(&child, &host, 5, 5) == 0);
    ASSERT(child.overlay_host == NULL);
}

TEST(follow_ignores_non_children)
{
    /* A window pinned to a DIFFERENT host isn't moved when this host moves. */
    Client host = {1, 0, 0, 800, 600, NULL, 0, 0};
    Client other = {2, 0, 0, 800, 600, NULL, 0, 0};
    Client child = {3, 0, 0, 100, 100, NULL, 0, 0};
    Client *all[] = {&host, &other, &child};
    overlay_pin(&child, &other, 0, 0);
    host.x = 999;
    overlay_follow(&host, all, 3);
    ASSERT(child.x == 0); /* untouched: child follows `other`, not `host` */
}

int
main(void)
{
    printf("=== Attached-Overlay Follow Tests ===\n");
    RUN_TEST(child_tracks_host_move);
    RUN_TEST(negative_offset_corner_pin);
    RUN_TEST(host_drives_multiple_children);
    RUN_TEST(refuses_self_pin);
    RUN_TEST(refuses_chain_pin);
    RUN_TEST(follow_ignores_non_children);

    printf("\n===================================\n");
    printf("Results: %d passed, %d total\n", test_passed, test_total);
    printf("Total assertion failures: %d\n", total_failures);
    printf("===================================\n");
    return total_failures > 0 ? 1 : 0;
}
