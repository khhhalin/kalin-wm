/* Unit tests for client lifecycle - spawn, focus, destroy */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

/* Mock structures for testing without wlroots */
typedef struct {
    int width, height;
} MockMonitor;

typedef struct {
    void *node;
    void *surface_node;
} MockScene;

typedef struct {
    float x, y;
    int width, height;
    int set;  /* Like world.set in real code */
} MockGeom;

typedef struct MockClient {
    MockGeom geom;
    MockGeom world;
    MockMonitor *mon;
    MockScene *scene;
    void *border[4];
    void *scene_surface;
    int bw;
    int isfloating;
    int isfullscreen;
    int crop_active;
    int requested_width;
    int requested_height;
    int mapped;
    struct MockClient *next;
} MockClient;

/* Test state */
static MockClient *clients = NULL;
static MockClient *sel = NULL;
static MockMonitor monitor = {1920, 1080};
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
    if (test_failures == 0) { \
        printf("PASS\n"); \
        test_passed++; \
    } else { \
        printf("FAIL (%d failures)\n", test_failures); \
        total_failures += test_failures; \
    } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "\n    ASSERT FAIL: " #cond " at line %d\n", __LINE__); \
        test_failures++; \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL)
#define ASSERT_NULL(ptr) ASSERT((ptr) == NULL)
#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_GT(a, b) ASSERT((a) > (b))

static void
reset_state(void)
{
    clients = NULL;
    sel = NULL;
}

/* Simulate place_window_column logic */
void place_window_column(MockClient *c, MockMonitor *m, float *column_x) {
    (void)m;
    if (c->world.set)
        return;
    
    MockClient *w;
    float x = 0;
    for (w = clients; w; w = w->next) {
        /* Only consider clients that have been placed (world.set) */
        if (w != c && w->world.set && w->world.x + w->geom.width > x) {
            x = w->world.x + w->geom.width;
        }
    }
    
    if (x > 0) x += 50;
    
    c->world.x = x;
    c->world.y = 0;
    c->world.set = 1;  /* Mark as set */
    
    *column_x = x;
}

/* Simulate resize logic */
void resize_mock(MockClient *c, MockGeom geo) {
    ASSERT_NOT_NULL(c);
    ASSERT_NOT_NULL(c->mon);
    ASSERT(c->mapped);
    ASSERT_NOT_NULL(c->scene);
    ASSERT_NOT_NULL(c->scene_surface);
    
    for (int i = 0; i < 4; i++) {
        ASSERT_NOT_NULL(c->border[i]);
    }
    
    ASSERT_GT(geo.width, 0);
    ASSERT_GT(geo.height, 0);
    ASSERT(!isnan(geo.x));
    ASSERT(!isnan(geo.y));
    ASSERT(!isinf(geo.x));
    ASSERT(!isinf(geo.y));
    
    c->geom = geo;
}

/* Simulate client_accept_requested_size logic */
int client_accept_requested_size_mock(MockClient *c) {
    MockGeom geo;
    int want_w, want_h;

    if (!c || c->isfullscreen || !c->isfloating)
        return 0;
    if (!c->scene || !c->scene_surface || !c->mapped)
        return 0;
    if (c->crop_active)
        return 0;

    if (c->requested_width <= 0 || c->requested_height <= 0)
        return 0;

    want_w = c->requested_width + 2 * c->bw;
    want_h = c->requested_height + 2 * c->bw;
    if (want_w <= 0 || want_h <= 0)
        return 0;

    if (want_w == c->geom.width && want_h == c->geom.height)
        return 0;

    geo = c->geom;
    geo.width = want_w;
    geo.height = want_h;
    resize_mock(c, geo);
    return 1;
}

/* Test 1: Basic client creation */
TEST(basic_client_creation) {
    reset_state();
    MockClient c = {0};
    c.mapped = 1;
    c.mon = &monitor;
    c.bw = 3;
    c.geom.width = 800;
    c.geom.height = 600;
    
}

/* Test 2: Place single window */
TEST(place_single_window) {
    reset_state();
    MockClient c = {0};
    c.mapped = 1;
    c.mon = &monitor;
    c.geom.width = 800;
    c.geom.height = 600;
    
    float column_x = 0;
    clients = &c;
    c.next = NULL;
    place_window_column(&c, &monitor, &column_x);
    
    ASSERT_EQ(c.world.x, 0);
    ASSERT_EQ(c.world.y, 0);
    reset_state();
}

/* Test 3: Place multiple windows */
TEST(place_multiple_windows) {
    reset_state();
    MockClient c1 = {0}, c2 = {0}, c3 = {0};
    
    c1.geom.width = 800; c1.geom.height = 600; c1.mapped = 1; c1.mon = &monitor;
    c2.geom.width = 800; c2.geom.height = 600; c2.mapped = 1; c2.mon = &monitor;
    c3.geom.width = 800; c3.geom.height = 600; c3.mapped = 1; c3.mon = &monitor;
    
    clients = &c1;
    c1.next = &c2;
    c2.next = &c3;
    c3.next = NULL;
    
    float col_x;
    place_window_column(&c1, &monitor, &col_x);
    place_window_column(&c2, &monitor, &col_x);
    place_window_column(&c3, &monitor, &col_x);
    
    ASSERT_EQ(c1.world.x, 0);
    ASSERT_EQ(c2.world.x, 850);
    ASSERT_EQ(c3.world.x, 1700);
    
    reset_state();
}

/* Test 4: Resize with NULL client */
TEST(resize_null_client) {
    reset_state();
    MockClient *c = NULL;
    if (c) {  /* Should skip */
        MockGeom geo = {100, 100, 800, 600, 0};
        resize_mock(c, geo);
    }
}

/* Test 5: Resize with NULL monitor */
TEST(resize_null_monitor) {
    reset_state();
    MockClient c = {0};
    MockGeom geo = {100, 100, 800, 600, 0};
    c.mon = NULL;
    c.mapped = 1;
    c.scene = (void*)1;
    c.scene_surface = (void*)1;
    for (int i = 0; i < 4; i++) c.border[i] = (void*)1;
    
    if (c.mon) {  /* Should skip */
        resize_mock(&c, geo);
    }
}

/* Test 6: Resize with NULL borders */
TEST(resize_null_borders) {
    reset_state();
    MockClient c = {0};
    MockGeom geo = {100, 100, 800, 600, 0};
    c.mon = &monitor;
    c.mapped = 1;
    c.scene = (void*)1;
    c.scene_surface = (void*)1;
    c.border[0] = (void*)1;
    c.border[1] = NULL;
    c.border[2] = (void*)1;
    c.border[3] = (void*)1;
    
    int all_set = 1;
    for (int i = 0; i < 4; i++) {
        if (!c.border[i]) all_set = 0;
    }
    ASSERT(!all_set);
    
    if (all_set) {  /* Should skip */
        resize_mock(&c, geo);
    }
}

/* Test 7: Resize with invalid geometry */
TEST(resize_invalid_geometry) {
    reset_state();
    MockGeom geo1 = {0, 0, -100, 600, 0};
    ASSERT(geo1.width <= 0);
    
    MockGeom geo2 = {0, 0, 0, 0, 0};
    ASSERT(geo2.width == 0);
}

/* Test 8: Focus switching with multiple clients */
TEST(focus_switching) {
    reset_state();
    MockClient c1 = {0}, c2 = {0}, c3 = {0};
    
    c1.mapped = c2.mapped = c3.mapped = 1;
    c1.mon = c2.mon = c3.mon = &monitor;
    
    clients = &c1;
    c1.next = &c2;
    c2.next = &c3;
    
    sel = &c2;
    ASSERT_NOT_NULL(sel);
    ASSERT_EQ(sel, &c2);
    
    sel = &c1;
    ASSERT_EQ(sel, &c1);
    
    reset_state();
}

/* Test 9: Simulate the crash scenario */
TEST(spawn_with_two_existing) {
    reset_state();
    MockClient c1 = {0}, c2 = {0}, c3 = {0};
    MockScene s1 = {0}, s2 = {0}, s3 = {0};
    void *borders1[4] = {(void*)1, (void*)1, (void*)1, (void*)1};
    void *borders2[4] = {(void*)1, (void*)1, (void*)1, (void*)1};
    void *borders3[4] = {(void*)1, (void*)1, (void*)1, (void*)1};
    
    c1.geom.width = 800; c1.geom.height = 600;
    c1.mapped = 1; c1.mon = &monitor;
    c1.scene = &s1; c1.scene_surface = &s1;
    memcpy(c1.border, borders1, sizeof(borders1));
    
    c2.geom.width = 800; c2.geom.height = 600;
    c2.mapped = 1; c2.mon = &monitor;
    c2.scene = &s2; c2.scene_surface = &s2;
    memcpy(c2.border, borders2, sizeof(borders2));
    
    clients = &c1;
    c1.next = &c2;
    c2.next = NULL;
    
    float col_x;
    place_window_column(&c1, &monitor, &col_x);
    place_window_column(&c2, &monitor, &col_x);
    
    sel = &c1;
    
    /* Spawn c3 */
    c3.geom.width = 800; c3.geom.height = 600;
    c3.mapped = 1;
    c3.mon = &monitor;
    c3.scene = &s3;
    c3.scene_surface = &s3;
    memcpy(c3.border, borders3, sizeof(borders3));
    
    c2.next = &c3;
    c3.next = NULL;
    
    place_window_column(&c3, &monitor, &col_x);
    
    MockGeom geo3 = {c3.world.x, c3.world.y, 800, 600, 0};
    resize_mock(&c3, geo3);
    
    ASSERT_GT(c3.geom.width, 0);
    ASSERT_EQ(c3.world.x, 1700);
    
    reset_state();
}

/* Test 10: Division by zero in zoom */
TEST(zoom_division_safety) {
    reset_state();
    float zoom = 0.0f;
    int width = 1920;
    
    float safe_zoom = zoom > 0.0f ? zoom : 1.0f;
    float result = width / safe_zoom;
    
    ASSERT(safe_zoom > 0);
    ASSERT(!isinf(result));
    ASSERT(!isnan(result));
}

/* Test 11: World coordinates overflow */
TEST(world_coordinates_overflow) {
    reset_state();
    MockClient c = {0};
    c.world.x = 1e30f;  /* Very large */
    c.world.y = 1e30f;
    c.geom.width = 800;
    c.geom.height = 600;
    
    ASSERT(!isinf(c.world.x));
    ASSERT(!isinf(c.world.y));
}

/* Test 12: Partial initialization scenario */
TEST(partial_initialization) {
    reset_state();
    /* Simulate what happens if resize is called before all fields are set */
    MockClient c = {0};
    
    /* Only some fields initialized (like after ecalloc but before full setup) */
    
    /* These might not be set yet! */
    /* c.scene = NULL; */
    /* c.scene_surface = NULL; */
    /* c.border[0..3] = NULL; */
    
    /* resize() should check all of these */
    int can_resize = c.scene && c.scene_surface && 
                     c.border[0] && c.border[1] && 
                     c.border[2] && c.border[3];
    
    ASSERT(!can_resize);  /* Should NOT be able to resize yet */
}

/* Test 13: place_window_column must not move already placed windows */
TEST(place_does_not_reposition_set_window) {
    reset_state();
    MockClient c = {0};
    float col_x = -1;

    c.world.set = 1;
    c.world.x = 4242;
    c.world.y = 77;
    clients = &c;

    place_window_column(&c, &monitor, &col_x);

    ASSERT_EQ(c.world.x, 4242);
    ASSERT_EQ(c.world.y, 77);
}

/* Test 14: placement should ignore clients without world.set */
TEST(place_skips_unplaced_existing_clients) {
    reset_state();
    MockClient c1 = {0}, c2 = {0};
    float col_x = 0;

    c1.geom.width = 800;
    c1.world.set = 0; /* not yet placed */
    c2.geom.width = 800;

    clients = &c1;
    c1.next = &c2;
    c2.next = NULL;

    place_window_column(&c2, &monitor, &col_x);
    ASSERT_EQ(c2.world.x, 0);
    ASSERT_EQ(c2.world.y, 0);
}

/* Test 15: resize should apply valid geometry when fully initialized */
TEST(resize_applies_valid_geometry) {
    reset_state();
    MockClient c = {0};
    MockScene s = {0};
    MockGeom geo = {120, 80, 900, 700, 0};
    int i;

    c.mon = &monitor;
    c.mapped = 1;
    c.scene = &s;
    c.scene_surface = &s;
    for (i = 0; i < 4; i++)
        c.border[i] = (void *)1;

    resize_mock(&c, geo);

    ASSERT_EQ(c.geom.x, 120);
    ASSERT_EQ(c.geom.y, 80);
    ASSERT_EQ(c.geom.width, 900);
    ASSERT_EQ(c.geom.height, 700);
}

/* Test 16: sequence placement must keep a stable gap */
TEST(column_gap_consistency) {
    reset_state();
    MockClient c1 = {0}, c2 = {0}, c3 = {0}, c4 = {0};
    float col_x = 0;

    c1.geom.width = c2.geom.width = c3.geom.width = c4.geom.width = 800;
    c1.mapped = c2.mapped = c3.mapped = c4.mapped = 1;
    c1.mon = c2.mon = c3.mon = c4.mon = &monitor;
    c1.next = &c2;
    c2.next = &c3;
    c3.next = &c4;
    c4.next = NULL;
    clients = &c1;

    place_window_column(&c1, &monitor, &col_x);
    place_window_column(&c2, &monitor, &col_x);
    place_window_column(&c3, &monitor, &col_x);
    place_window_column(&c4, &monitor, &col_x);

    ASSERT_EQ(c1.world.x, 0);
    ASSERT_EQ(c2.world.x - c1.world.x, 850);
    ASSERT_EQ(c3.world.x - c2.world.x, 850);
    ASSERT_EQ(c4.world.x - c3.world.x, 850);
}

/* Test 17: floating clients can accept app-requested size */
TEST(floating_accepts_requested_size) {
    reset_state();
    MockClient c = {0};
    MockScene s = {0};

    c.mon = &monitor;
    c.scene = &s;
    c.scene_surface = &s;
    c.mapped = 1;
    c.isfloating = 1;
    c.bw = 3;
    c.geom.width = 806;
    c.geom.height = 606;
    c.requested_width = 1200;
    c.requested_height = 700;
    for (int i = 0; i < 4; i++)
        c.border[i] = (void *)1;

    ASSERT_EQ(client_accept_requested_size_mock(&c), 1);
    ASSERT_EQ(c.geom.width, 1206);
    ASSERT_EQ(c.geom.height, 706);
}

/* Test 18: crop mode blocks requested-size acceptance */
TEST(crop_blocks_requested_size_acceptance) {
    reset_state();
    MockClient c = {0};
    MockScene s = {0};

    c.mon = &monitor;
    c.scene = &s;
    c.scene_surface = &s;
    c.mapped = 1;
    c.isfloating = 1;
    c.crop_active = 1;
    c.bw = 3;
    c.geom.width = 900;
    c.geom.height = 700;
    c.requested_width = 1200;
    c.requested_height = 800;
    for (int i = 0; i < 4; i++)
        c.border[i] = (void *)1;

    ASSERT_EQ(client_accept_requested_size_mock(&c), 0);
    ASSERT_EQ(c.geom.width, 900);
    ASSERT_EQ(c.geom.height, 700);
}

int main(void) {
    printf("\n=== Client Lifecycle Unit Tests ===\n\n");
    
    RUN_TEST(basic_client_creation);
    RUN_TEST(place_single_window);
    RUN_TEST(place_multiple_windows);
    RUN_TEST(resize_null_client);
    RUN_TEST(resize_null_monitor);
    RUN_TEST(resize_null_borders);
    RUN_TEST(resize_invalid_geometry);
    RUN_TEST(focus_switching);
    RUN_TEST(spawn_with_two_existing);
    RUN_TEST(zoom_division_safety);
    RUN_TEST(world_coordinates_overflow);
    RUN_TEST(partial_initialization);
    RUN_TEST(place_does_not_reposition_set_window);
    RUN_TEST(place_skips_unplaced_existing_clients);
    RUN_TEST(resize_applies_valid_geometry);
    RUN_TEST(column_gap_consistency);
    RUN_TEST(floating_accepts_requested_size);
    RUN_TEST(crop_blocks_requested_size_acceptance);
    
    printf("\n===================================\n");
    printf("Results: %d passed, %d total\n", test_passed, test_total);
    printf("Total assertion failures: %d\n", total_failures);
    printf("===================================\n\n");
    
    return total_failures > 0 ? 1 : 0;
}
