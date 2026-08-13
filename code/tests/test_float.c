/* Unit tests for the float-under-cursor placement math (layout Phase 4 —
 * the floatprep branch in dwl.c's mapnotify(), obsidian/implementation/
 * float-overlay.md). Like test_rail.c, the real branch calls into wlroots
 * (SCREEN_TO_WORLD, resize) and dwl.c globals that can't link standalone, so
 * this reimplements the *pure geometry* worth testing — the non-obscuring
 * offset rule (which corner sits near the cursor, expanding toward the roomier
 * side) — verbatim against plain ints, and checks the four cursor quadrants.
 *
 * The routing decision itself (floatprep_consume() picks this branch over the
 * rail) is a one-shot table match tested by floatprep's own consume/register
 * being a verbatim twin of dockprep; the branch selection is exercised at
 * runtime (float-next -> window maps off-rail). */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define FLOAT_CURSOR_MARGIN 24

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

/* ---- verbatim mirror of the float-under-cursor offset rule ------------- *
 * Given the cursor at (cx,cy) in world coords, the monitor's world extent
 * (mx,my,mw,mh), and the window size (dw,dh), pick the window's top-left so the
 * clicked point (cx,cy) stays visible: the window expands toward whichever side
 * of the cursor has more room, kept FLOAT_CURSOR_MARGIN clear of the cursor. */
static void
float_place(int cx, int cy, int mx, int my, int mw, int mh,
            int dw, int dh, int *out_x, int *out_y)
{
    if ((mx + mw) - cx >= cx - mx)
        *out_x = cx + FLOAT_CURSOR_MARGIN;
    else
        *out_x = cx - FLOAT_CURSOR_MARGIN - dw;
    if ((my + mh) - cy >= cy - my)
        *out_y = cy + FLOAT_CURSOR_MARGIN;
    else
        *out_y = cy - FLOAT_CURSOR_MARGIN - dh;
}

/* ---- tests ------------------------------------------------------------- */

TEST(cursor_top_left_expands_down_right)
{
    /* Cursor near the monitor's top-left: more room down and right, so the
     * window's top-left sits just past the cursor (down-right). */
    int x, y;
    float_place(100, 100, 0, 0, 1600, 900, 400, 300, &x, &y);
    ASSERT(x == 124);   /* 100 + margin */
    ASSERT(y == 124);
    /* clicked point (100,100) is left of and above the window -> visible */
    ASSERT(x > 100 && y > 100);
}

TEST(cursor_bottom_right_expands_up_left)
{
    /* Cursor near the monitor's bottom-right: more room up and left, so the
     * window's bottom-right sits just before the cursor (up-left corner). */
    int x, y;
    float_place(1500, 850, 0, 0, 1600, 900, 400, 300, &x, &y);
    ASSERT(x == 1500 - 24 - 400);  /* 1076: right edge = 1476, left of cursor */
    ASSERT(y == 850 - 24 - 300);   /* 526 */
    ASSERT(x + 400 < 1500 && y + 300 < 850); /* window clears the click */
}

TEST(cursor_top_right_expands_down_left)
{
    int x, y;
    float_place(1500, 100, 0, 0, 1600, 900, 400, 300, &x, &y);
    ASSERT(x == 1500 - 24 - 400);  /* room to the left in x */
    ASSERT(y == 124);              /* room below in y */
}

TEST(cursor_centered_prefers_down_right)
{
    /* Dead center: the >= tie-break in the rule sends it down-right. */
    int x, y;
    float_place(800, 450, 0, 0, 1600, 900, 400, 300, &x, &y);
    ASSERT(x == 824);
    ASSERT(y == 474);
}

TEST(monitor_offset_respected)
{
    /* A monitor whose world origin isn't 0,0: room test is relative to mx/my,
     * not absolute — cursor near this monitor's right edge expands left. */
    int x, y;
    float_place(2500, 500, 1600, 0, 1600, 900, 400, 300, &x, &y);
    ASSERT(x == 2500 - 24 - 400);  /* (3200-2500)=700 vs (2500-1600)=900 -> left */
    ASSERT(y == 500 - 24 - 300);   /* (900-500)=400 vs 500 -> up: 176 */
}

int
main(void)
{
    printf("=== Float-under-cursor Placement Tests ===\n");
    RUN_TEST(cursor_top_left_expands_down_right);
    RUN_TEST(cursor_bottom_right_expands_up_left);
    RUN_TEST(cursor_top_right_expands_down_left);
    RUN_TEST(cursor_centered_prefers_down_right);
    RUN_TEST(monitor_offset_respected);

    printf("\n===================================\n");
    printf("Results: %d passed, %d total\n", test_passed, test_total);
    printf("Total assertion failures: %d\n", total_failures);
    printf("===================================\n");
    return total_failures > 0 ? 1 : 0;
}
