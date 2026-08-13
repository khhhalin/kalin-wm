/* Unit tests for the rail linkage ordering (layout Phase 2 —
 * code/src/modules/layout/rail.c, obsidian/implementation/rail.md).
 *
 * The real rail_insert_after()/rail_remove()/rail_swap_dir() call into wlroots
 * geometry (client_set_target_geom, resize) and dwl.c globals that can't be
 * linked standalone, so — like the removed test_connection_graph.c did for the
 * graph swaps — this file reimplements the *pure pointer bookkeeping* (the part
 * worth testing: who ends up next to whom, and rail_head tracking) verbatim
 * against a minimal mock Client, and exercises the insert/close/swap orderings.
 * The geometry-shift side (rail_open_gap_after / gap-close) is left to the
 * runtime smoke test — it's a mechanical dx walk with nothing branchy to unit. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef struct MockClient {
    int id;                 /* stable label so tests can spell out orderings */
    int x;                  /* world x — exercised by the grow-push math below */
    int width;
    int allow_overlap;      /* grow-over-successors flag (Phase 3) */
    struct MockClient *rail_prev, *rail_next;
} Client;

#define SPAWN_GAP 20

static Client *rail_head;

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

/* ---- verbatim mirror of rail.c's linkage (no geometry) ---------------- */

static void
rail_insert_after(Client *p, Client *c)
{
    if (!c)
        return;

    if (!p) {
        if (!rail_head)
            rail_head = c;
        else
            for (p = rail_head; p->rail_next; p = p->rail_next)
                ;
        if (!p) {
            c->rail_prev = c->rail_next = NULL;
            return;
        }
    } else if (!p->rail_prev && !p->rail_next && rail_head != p) {
        if (!rail_head) {
            rail_head = p;
            p->rail_prev = p->rail_next = NULL;
        } else {
            for (p = rail_head; p->rail_next; p = p->rail_next)
                ;
        }
    }

    c->rail_prev = p;
    c->rail_next = p->rail_next;
    if (p->rail_next)
        p->rail_next->rail_prev = c;
    p->rail_next = c;
}

static void
rail_remove(Client *c)
{
    Client *prev, *next;

    if (!c || (!c->rail_prev && !c->rail_next && rail_head != c))
        return;

    prev = c->rail_prev;
    next = c->rail_next;

    if (prev)
        prev->rail_next = next;
    if (next)
        next->rail_prev = prev;
    if (rail_head == c)
        rail_head = next;

    c->rail_prev = c->rail_next = NULL;
}

static void
rail_swap(Client *c, int right) /* right: 1 = swap with rail_next, 0 = rail_prev */
{
    Client *n = right ? c->rail_next : c->rail_prev;
    Client *a_prev, *a_next, *b_prev, *b_next;

    if (!n)
        return;

    a_prev = c->rail_prev; a_next = c->rail_next;
    b_prev = n->rail_prev; b_next = n->rail_next;

    if (a_next == n) {
        c->rail_prev = n; c->rail_next = b_next;
        n->rail_prev = a_prev; n->rail_next = c;
        if (a_prev) a_prev->rail_next = n;
        if (b_next) b_next->rail_prev = c;
    } else if (b_next == c) {
        n->rail_prev = c; n->rail_next = a_next;
        c->rail_prev = b_prev; c->rail_next = n;
        if (b_prev) b_prev->rail_next = c;
        if (a_next) a_next->rail_prev = n;
    } else {
        c->rail_prev = b_prev; c->rail_next = b_next;
        n->rail_prev = a_prev; n->rail_next = a_next;
        if (b_prev) b_prev->rail_next = c;
        if (b_next) b_next->rail_prev = c;
        if (a_prev) a_prev->rail_next = n;
        if (a_next) a_next->rail_prev = n;
    }

    if (rail_head == c) rail_head = n;
    else if (rail_head == n) rail_head = c;
}

/* Mirror of rail.c's grow-push (Phase 3): the real one slides successors via
 * client_set_target_geom() on their world geom; here it moves the mock's ->x.
 * Same overlap-based math — push the whole successor chain right by the amount
 * `grown`'s right edge (plus the gap) overruns its first successor's left. */
static void
rail_push_growth(Client *grown)
{
    Client *first, *s;
    int overlap;

    if (!grown || grown->allow_overlap)
        return;
    if (!grown->rail_prev && !grown->rail_next && rail_head != grown)
        return;

    first = grown->rail_next;
    if (!first)
        return;

    overlap = (grown->x + grown->width + SPAWN_GAP) - first->x;
    if (overlap > 0)
        for (s = grown->rail_next; s; s = s->rail_next)
            s->x += overlap;
}

/* ---- helpers ---------------------------------------------------------- */

static Client *
mk(int id)
{
    Client *c = calloc(1, sizeof(*c));
    c->id = id;
    c->width = 100;
    return c;
}

/* Lay the current rail out left-to-right at x=0, each member SPAWN_GAP past the
 * previous one's right edge — the gap-free row the grow-push then perturbs. */
static void
layout_rail(void)
{
    Client *c;
    int x = 0;
    for (c = rail_head; c; c = c->rail_next) {
        c->x = x;
        x += c->width + SPAWN_GAP;
    }
}

/* Walk the rail head->tail into a string of ids like "1-2-3"; also verifies
 * rail_prev is the exact inverse of rail_next at every step. */
static void
order(char *out, size_t n)
{
    Client *c, *prev = NULL;
    size_t off = 0;
    out[0] = '\0';
    for (c = rail_head; c; prev = c, c = c->rail_next) {
        int w = snprintf(out + off, n - off, "%s%d", off ? "-" : "", c->id);
        if (w > 0) off += (size_t)w;
        if (c->rail_prev != prev) { /* linkage inconsistency */
            snprintf(out + off, n - off, "!BADPREV");
            return;
        }
    }
}

static void
reset(void)
{
    rail_head = NULL;
}

/* ---- tests ------------------------------------------------------------ */

TEST(insert_seeds_rail_with_parent_and_child)
{
    /* First keyboard spawn: parent A is off-rail, child B inserts after it —
     * both join, A as head. */
    Client *a = mk(1), *b = mk(2);
    char buf[64];
    reset();
    rail_insert_after(a, b);
    order(buf, sizeof(buf));
    ASSERT(strcmp(buf, "1-2") == 0);
    ASSERT(rail_head == a);
    free(a); free(b);
}

TEST(insert_after_tail_appends)
{
    Client *a = mk(1), *b = mk(2), *c = mk(3);
    char buf[64];
    reset();
    rail_insert_after(a, b);      /* 1-2 */
    rail_insert_after(b, c);      /* 1-2-3 */
    order(buf, sizeof(buf));
    ASSERT(strcmp(buf, "1-2-3") == 0);
    free(a); free(b); free(c);
}

TEST(insert_in_middle_splices)
{
    /* 1-2-3, spawn X off the focused middle (2): X lands between 2 and 3. */
    Client *a = mk(1), *b = mk(2), *c = mk(3), *x = mk(9);
    char buf[64];
    reset();
    rail_insert_after(a, b);
    rail_insert_after(b, c);
    rail_insert_after(b, x);      /* after 2 */
    order(buf, sizeof(buf));
    ASSERT(strcmp(buf, "1-2-9-3") == 0);
    free(a); free(b); free(c); free(x);
}

TEST(remove_middle_closes_and_relinks)
{
    Client *a = mk(1), *b = mk(2), *c = mk(3);
    char buf[64];
    reset();
    rail_insert_after(a, b);
    rail_insert_after(b, c);      /* 1-2-3 */
    rail_remove(b);               /* -> 1-3 */
    order(buf, sizeof(buf));
    ASSERT(strcmp(buf, "1-3") == 0);
    ASSERT(b->rail_prev == NULL && b->rail_next == NULL); /* nulled, no dangle */
    free(a); free(b); free(c);
}

TEST(remove_head_updates_rail_head)
{
    Client *a = mk(1), *b = mk(2);
    char buf[64];
    reset();
    rail_insert_after(a, b);      /* 1-2, head=1 */
    rail_remove(a);               /* -> 2, head must move to 2 */
    order(buf, sizeof(buf));
    ASSERT(strcmp(buf, "2") == 0);
    ASSERT(rail_head == b);
    free(a); free(b);
}

TEST(remove_last_empties_rail)
{
    Client *a = mk(1), *b = mk(2);
    reset();
    rail_insert_after(a, b);
    rail_remove(a);
    rail_remove(b);
    ASSERT(rail_head == NULL);
    free(a); free(b);
}

TEST(remove_offrail_is_noop)
{
    Client *a = mk(1), *b = mk(2), *lone = mk(7);
    char buf[64];
    reset();
    rail_insert_after(a, b);      /* 1-2 */
    rail_remove(lone);            /* never on the rail: must not touch head */
    order(buf, sizeof(buf));
    ASSERT(strcmp(buf, "1-2") == 0);
    ASSERT(rail_head == a);
    free(a); free(b); free(lone);
}

TEST(swap_right_middle)
{
    /* 1-2-3, focus 2, swap right with 3 -> 1-3-2. */
    Client *a = mk(1), *b = mk(2), *c = mk(3);
    char buf[64];
    reset();
    rail_insert_after(a, b);
    rail_insert_after(b, c);
    rail_swap(b, 1);
    order(buf, sizeof(buf));
    ASSERT(strcmp(buf, "1-3-2") == 0);
    free(a); free(b); free(c);
}

TEST(swap_left_head_moves_head)
{
    /* 1-2, focus 2, swap left with 1 -> 2-1, head becomes 2. */
    Client *a = mk(1), *b = mk(2);
    char buf[64];
    reset();
    rail_insert_after(a, b);
    rail_swap(b, 0);
    order(buf, sizeof(buf));
    ASSERT(strcmp(buf, "2-1") == 0);
    ASSERT(rail_head == b);
    free(a); free(b);
}

TEST(swap_at_end_is_noop)
{
    /* 1-2, focus 2, swap right (no rail_next) -> unchanged. */
    Client *a = mk(1), *b = mk(2);
    char buf[64];
    reset();
    rail_insert_after(a, b);
    rail_swap(b, 1);
    order(buf, sizeof(buf));
    ASSERT(strcmp(buf, "1-2") == 0);
    ASSERT(rail_head == a);
    free(a); free(b);
}

/* ---- grow-push tests (Phase 3) ---------------------------------------- */

TEST(grow_pushes_single_successor)
{
    /* 1-2 gap-free (2 at x=120). Grow 1 by 80 -> its right edge overruns 2 by
     * 80, so 2 slides right by exactly 80 (200), restoring the gap. */
    Client *a = mk(1), *b = mk(2);
    reset();
    rail_insert_after(a, b);
    layout_rail();                 /* 1@0(w100), 2@120 */
    a->width += 80;                /* 1 now 180 wide, right edge 180 */
    rail_push_growth(a);
    ASSERT(b->x == 200);           /* 180 + SPAWN_GAP(20) */
    free(a); free(b);
}

TEST(grow_pushes_whole_chain_by_overlap)
{
    /* 1-2-3 gap-free (2@120, 3@240). Grow 1 by 50 -> overlap 50; both
     * successors shift right by 50, preserving their mutual spacing. */
    Client *a = mk(1), *b = mk(2), *c = mk(3);
    reset();
    rail_insert_after(a, b);
    rail_insert_after(b, c);
    layout_rail();                 /* 1@0, 2@120, 3@240 */
    a->width += 50;                /* right edge 150; 150+20 - 120 = 50 overlap */
    rail_push_growth(a);
    ASSERT(b->x == 170);           /* 120 + 50 */
    ASSERT(c->x == 290);           /* 240 + 50, spacing preserved */
    free(a); free(b); free(c);
}

TEST(grow_middle_pushes_only_later)
{
    /* Growing a middle member pushes only its successors, not its predecessor. */
    Client *a = mk(1), *b = mk(2), *c = mk(3);
    reset();
    rail_insert_after(a, b);
    rail_insert_after(b, c);
    layout_rail();                 /* 1@0, 2@120, 3@240 */
    b->width += 60;                /* 2 right edge 280; 280+20 - 240 = 60 overlap */
    rail_push_growth(b);
    ASSERT(a->x == 0);             /* predecessor untouched */
    ASSERT(c->x == 300);           /* 240 + 60 */
    free(a); free(b); free(c);
}

TEST(nogrow_with_slack_does_not_push)
{
    /* Successor sits with slack (further right than the minimum gap): a growth
     * that stays within that slack leaves it put, and the push is idempotent. */
    Client *a = mk(1), *b = mk(2);
    reset();
    rail_insert_after(a, b);
    layout_rail();                 /* 1@0(w100), 2@120 */
    b->x = 200;                    /* hand-nudged right: 80px of slack */
    a->width += 40;                /* right edge 140; 140+20 - 200 = -40 (no overlap) */
    rail_push_growth(a);
    ASSERT(b->x == 200);           /* still fits, unmoved */
    rail_push_growth(a);           /* idempotent */
    ASSERT(b->x == 200);
    free(a); free(b);
}

TEST(allow_overlap_suppresses_push)
{
    /* Flagged head grows OVER its successor: no shift at all (Phase 3 semantics). */
    Client *a = mk(1), *b = mk(2);
    reset();
    rail_insert_after(a, b);
    layout_rail();                 /* 1@0, 2@120 */
    a->allow_overlap = 1;
    a->width += 200;               /* huge overrun, but push suppressed */
    rail_push_growth(a);
    ASSERT(b->x == 120);           /* unmoved — 1 overlaps it */
    free(a); free(b);
}

TEST(grow_tail_is_noop)
{
    /* Growing the last member has no successors to push. */
    Client *a = mk(1), *b = mk(2);
    reset();
    rail_insert_after(a, b);
    layout_rail();
    b->width += 100;
    rail_push_growth(b);           /* must not crash / touch anything */
    ASSERT(a->x == 0 && b->x == 120);
    free(a); free(b);
}

TEST(grow_offrail_is_noop)
{
    /* A window never on the rail: push is a no-op even with a stray width. */
    Client *a = mk(1), *b = mk(2), *lone = mk(7);
    reset();
    rail_insert_after(a, b);
    layout_rail();
    lone->x = 500; lone->width = 400;
    rail_push_growth(lone);        /* off-rail guard */
    ASSERT(a->x == 0 && b->x == 120);
    free(a); free(b); free(lone);
}

int
main(void)
{
    printf("=== Rail Linkage Tests ===\n");
    RUN_TEST(insert_seeds_rail_with_parent_and_child);
    RUN_TEST(insert_after_tail_appends);
    RUN_TEST(insert_in_middle_splices);
    RUN_TEST(remove_middle_closes_and_relinks);
    RUN_TEST(remove_head_updates_rail_head);
    RUN_TEST(remove_last_empties_rail);
    RUN_TEST(remove_offrail_is_noop);
    RUN_TEST(swap_right_middle);
    RUN_TEST(swap_left_head_moves_head);
    RUN_TEST(swap_at_end_is_noop);
    RUN_TEST(grow_pushes_single_successor);
    RUN_TEST(grow_pushes_whole_chain_by_overlap);
    RUN_TEST(grow_middle_pushes_only_later);
    RUN_TEST(nogrow_with_slack_does_not_push);
    RUN_TEST(allow_overlap_suppresses_push);
    RUN_TEST(grow_tail_is_noop);
    RUN_TEST(grow_offrail_is_noop);

    printf("\n===================================\n");
    printf("Results: %d passed, %d total\n", test_passed, test_total);
    printf("Total assertion failures: %d\n", total_failures);
    printf("===================================\n");
    return total_failures > 0 ? 1 : 0;
}
