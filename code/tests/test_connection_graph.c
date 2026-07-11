/* Standalone logic tests for the connection-graph core in dwl.c:
 * connect_clients() (including its double-link guard), close_gap(), a
 * pointer-keyed stand-in for sever_connection() (the real function's id ->
 * Client lookup walks the global `clients` wl_list, not worth reimplementing
 * here — the interesting logic is the slot-clearing loop, which this tests
 * directly against Client pointers), and swap_neighbor_dir()'s three-way
 * connection-transfer bookkeeping.
 *
 * dwl.c is a monolithic compositor binary that can't be linked standalone
 * (it needs a running wlroots backend), so this test reimplements the exact
 * graph math verbatim against a minimal Client stand-in, the same technique
 * already used by test_growth_overlap.c. Keep this in sync with dwl.c if
 * that logic changes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#define M_PI 3.14159265358979323846
#define SPAWN_GAP 20
enum Octant { OCT_N, OCT_NE, OCT_E, OCT_SE, OCT_S, OCT_SW, OCT_W, OCT_NW };
enum { DIR_LEFT, DIR_RIGHT, DIR_UP, DIR_DOWN };

struct wlr_box { int x, y, width, height; };

typedef struct Client {
	int id;
	struct wlr_box geom;
	struct Client *neighbor[8];
} Client;

static int resize_calls = 0;

static void
resize(Client *c, struct wlr_box geo, int interact)
{
	(void)interact;
	c->geom = geo;
	resize_calls++;
}

/* Stand-in for client_set_target_geom(): swap_neighbor_dir()'s real
 * implementation glides via a spring animation; only the final position
 * matters for these tests, so this just sets it directly. */
static void
client_set_target_geom(Client *c, struct wlr_box geo)
{
	c->geom = geo;
}

static int
opposite_octant(int oct)
{
	return (oct + 4) % 8;
}

static int
octant_from_delta(float dx, float dy)
{
	static const float step = (float)M_PI / 4.0f;
	float angle = atan2f(dx, -dy);
	int oct;
	if (angle < 0)
		angle += 2.0f * (float)M_PI;
	oct = (int)lroundf(angle / step) % 8;
	return oct;
}

static int
collect_component(Client *start, Client **out, int max)
{
	int count = 0, head = 0;

	if (!start || max <= 0)
		return 0;
	out[count++] = start;
	while (head < count) {
		Client *cur = out[head++];
		int i, j, found;

		for (i = 0; i < 8; i++) {
			Client *n = cur->neighbor[i];
			if (!n)
				continue;
			found = 0;
			for (j = 0; j < count; j++)
				if (out[j] == n) { found = 1; break; }
			if (!found && count < max)
				out[count++] = n;
		}
	}
	return count;
}

static int
clients_already_linked(Client *a, Client *b)
{
	int i;
	for (i = 0; i < 8; i++)
		if (a->neighbor[i] == b)
			return 1;
	return 0;
}

static void
connect_clients(Client *a, Client *b)
{
	float acx, acy, bcx, bcy;
	int oct_ab, oct_ba;

	if (!a || !b || a == b)
		return;
	if (clients_already_linked(a, b))
		return;

	acx = a->geom.x + a->geom.width / 2.0f;
	acy = a->geom.y + a->geom.height / 2.0f;
	bcx = b->geom.x + b->geom.width / 2.0f;
	bcy = b->geom.y + b->geom.height / 2.0f;

	oct_ab = octant_from_delta(bcx - acx, bcy - acy);
	oct_ba = opposite_octant(oct_ab);

	if (a->neighbor[oct_ab] || b->neighbor[oct_ba])
		return;

	a->neighbor[oct_ab] = b;
	b->neighbor[oct_ba] = a;
}

/* Reimplementation of connect_pick_complete()'s guard logic (connection_
 * graph.c): no-op if nothing's pending, no-op if the target is the source
 * itself, otherwise defer to connect_clients() (already covered by its own
 * tests above for the occupied-slot/already-linked cases) and clear pending.
 * Takes a pointer to the pending slot rather than reaching for a static, to
 * mirror the real function's file-static `pending_connect_source` without
 * needing one here. */
static void
connect_pick_complete(Client **pending, Client *target)
{
	if (!*pending || !target || target == *pending)
		return;
	connect_clients(*pending, target);
	*pending = NULL;
}

/* Pointer-keyed stand-in for sever_connection(uint32_t, uint32_t)'s
 * slot-clearing loop. */
static void
sever_connection_ptr(Client *a, Client *b)
{
	int i;
	if (!a || !b)
		return;
	for (i = 0; i < 8; i++) {
		if (a->neighbor[i] == b)
			a->neighbor[i] = NULL;
		if (b->neighbor[i] == a)
			b->neighbor[i] = NULL;
	}
}

static void
close_gap(Client *a, Client *b)
{
	float acx, acy, bcx, bcy, dx, dy;
	int oct_ab, opp, shift_x = 0, shift_y = 0, gap;
	Client *component[256];
	int ncomp, k;

	if (!a || !b)
		return;

	acx = a->geom.x + a->geom.width / 2.0f;
	acy = a->geom.y + a->geom.height / 2.0f;
	bcx = b->geom.x + b->geom.width / 2.0f;
	bcy = b->geom.y + b->geom.height / 2.0f;
	dx = bcx - acx;
	dy = bcy - acy;

	oct_ab = octant_from_delta(dx, dy);
	opp = opposite_octant(oct_ab);
	if (b->neighbor[opp] != a)
		return;

	if (fabsf(dx) >= fabsf(dy)) {
		if (dx > 0) {
			gap = b->geom.x - (a->geom.x + a->geom.width);
			shift_x = -(gap - SPAWN_GAP);
		} else {
			gap = a->geom.x - (b->geom.x + b->geom.width);
			shift_x = gap - SPAWN_GAP;
		}
	} else {
		if (dy > 0) {
			gap = b->geom.y - (a->geom.y + a->geom.height);
			shift_y = -(gap - SPAWN_GAP);
		} else {
			gap = a->geom.y - (b->geom.y + b->geom.height);
			shift_y = gap - SPAWN_GAP;
		}
	}
	if (shift_x == 0 && shift_y == 0)
		return;

	b->neighbor[opp] = NULL;
	ncomp = collect_component(b, component, (int)(sizeof(component) / sizeof(component[0])));
	b->neighbor[opp] = a;

	for (k = 0; k < ncomp; k++) {
		struct wlr_box nb = component[k]->geom;
		nb.x += shift_x;
		nb.y += shift_y;
		resize(component[k], nb, 0);
	}
}

/* Verbatim reimplementation of swap_neighbor_dir()'s connection-graph
 * bookkeeping (the geometry-swap and camera-follow parts are omitted —
 * dwl.c-only concerns, not graph logic). */
static void
swap_neighbor_dir(Client *c, int dir)
{
	Client *n;
	int oct, opp;
	int cx, cy;

	switch (dir) {
	case DIR_LEFT:  oct = OCT_W; break;
	case DIR_RIGHT: oct = OCT_E; break;
	case DIR_UP:    oct = OCT_N; break;
	case DIR_DOWN:  oct = OCT_S; break;
	default: return;
	}

	n = c->neighbor[oct];
	if (!n)
		return;
	opp = opposite_octant(oct);

	cx = c->geom.x; cy = c->geom.y;
	client_set_target_geom(c, (struct wlr_box){
		.x = n->geom.x, .y = n->geom.y,
		.width = c->geom.width, .height = c->geom.height});
	client_set_target_geom(n, (struct wlr_box){
		.x = cx, .y = cy,
		.width = n->geom.width, .height = n->geom.height});

	c->neighbor[oct] = NULL;
	n->neighbor[opp] = NULL;

	if (c->neighbor[opp] && c->neighbor[opp] != n) {
		Client *third = c->neighbor[opp];
		int opp2 = opposite_octant(opp);
		int k;
		for (k = 0; k < 8; k++)
			if (third->neighbor[k] == c)
				third->neighbor[k] = NULL;
		if (!n->neighbor[opp]) {
			n->neighbor[opp] = third;
			third->neighbor[opp2] = n;
		}
	}
	c->neighbor[opp] = n;

	if (n->neighbor[oct] && n->neighbor[oct] != c) {
		Client *third = n->neighbor[oct];
		int opp2 = opposite_octant(oct);
		int k;
		for (k = 0; k < 8; k++)
			if (third->neighbor[k] == n)
				third->neighbor[k] = NULL;
		if (!c->neighbor[oct]) {
			c->neighbor[oct] = third;
			third->neighbor[opp2] = c;
		}
	}
	n->neighbor[oct] = c;

	/* Regression fix (2026-07-09): c and n traded positions exactly, so any
	 * neighbor on an axis other than the swap axis is still standing where
	 * it always was, now adjacent to whichever of the pair moved into that
	 * spot — transfer those links too, or they stay stale. See the matching
	 * comment (and full rationale) in the real swap_neighbor_dir(),
	 * modules/layout/connection_graph.c. */
	{
		int i;
		for (i = 0; i < 8; i++) {
			Client *co, *no;
			int opp_i, k;

			if (i == oct || i == opp)
				continue;
			co = c->neighbor[i];
			no = n->neighbor[i];
			if (co == no)
				continue;
			opp_i = opposite_octant(i);

			if (co) {
				for (k = 0; k < 8; k++)
					if (co->neighbor[k] == c)
						co->neighbor[k] = NULL;
				co->neighbor[opp_i] = n;
			}
			if (no) {
				for (k = 0; k < 8; k++)
					if (no->neighbor[k] == n)
						no->neighbor[k] = NULL;
				no->neighbor[opp_i] = c;
			}
			c->neighbor[i] = no;
			n->neighbor[i] = co;
		}
	}
}

static int failures = 0;

#define CHECK(cond, msg) do { \
	if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
} while (0)

static void
test_connect_basic_link_both_sides(void)
{
	Client a = { 1, {0, 0, 100, 100}, {0} };
	Client b = { 2, {200, 0, 100, 100}, {0} };

	printf("Running connect_basic_link_both_sides...\n");
	connect_clients(&a, &b);
	CHECK(a.neighbor[OCT_E] == &b, "a should link to b on its East slot");
	CHECK(b.neighbor[OCT_W] == &a, "b should link back to a on its West slot");
	printf(a.neighbor[OCT_E] == &b && b.neighbor[OCT_W] == &a ? "  PASS\n" : "  (see failures above)\n");
}

static void
test_connect_noop_if_slot_occupied(void)
{
	Client a = { 1, {0, 0, 100, 100}, {0} };
	Client b = { 2, {200, 0, 100, 100}, {0} };
	Client x = { 3, {200, 300, 100, 100}, {0} };

	a.neighbor[OCT_E] = &b; b.neighbor[OCT_W] = &a;

	printf("Running connect_noop_if_slot_occupied...\n");
	connect_clients(&a, &x); /* a's East slot is already taken by b */
	CHECK(a.neighbor[OCT_E] == &b, "a's East slot must still point at b, not x");
	CHECK(x.neighbor[OCT_W] == NULL, "x must not have been linked at all");
	printf(a.neighbor[OCT_E] == &b && x.neighbor[OCT_W] == NULL ? "  PASS\n" : "  (see failures above)\n");
}

static void
test_connect_no_double_link_when_already_connected(void)
{
	/* Regression test: a and b are already linked (e.g. diagonally, on
	 * OCT_NE/OCT_SW, left over from before one of them moved). Reconnecting
	 * them per their *current* geometry (which now lines up as due
	 * East/West) must not add a second edge on top of the existing one. */
	Client a = { 1, {0, 0, 100, 100}, {0} };
	Client b = { 2, {200, 0, 100, 100}, {0} };

	a.neighbor[OCT_NE] = &b; b.neighbor[OCT_SW] = &a;

	printf("Running connect_no_double_link_when_already_connected...\n");
	connect_clients(&a, &b);
	CHECK(a.neighbor[OCT_NE] == &b, "the original NE edge must be untouched");
	CHECK(a.neighbor[OCT_E] == NULL, "must not add a second edge on the East slot");
	CHECK(b.neighbor[OCT_E] == NULL, "b must not gain a stray edge either");
	printf(a.neighbor[OCT_E] == NULL && b.neighbor[OCT_E] == NULL ? "  PASS\n" : "  (see failures above)\n");
}

static void
test_sever_clears_both_sides(void)
{
	Client a = { 1, {0, 0, 100, 100}, {0} };
	Client b = { 2, {200, 0, 100, 100}, {0} };
	a.neighbor[OCT_E] = &b; b.neighbor[OCT_W] = &a;

	printf("Running sever_clears_both_sides...\n");
	sever_connection_ptr(&a, &b);
	CHECK(a.neighbor[OCT_E] == NULL, "a's slot must be cleared");
	CHECK(b.neighbor[OCT_W] == NULL, "b's slot must be cleared");
	printf(a.neighbor[OCT_E] == NULL && b.neighbor[OCT_W] == NULL ? "  PASS\n" : "  (see failures above)\n");
}

static void
test_close_gap_shifts_downstream_component(void)
{
	/* a at x=0..100, b at x=300..400 (200px gap between edges, way more
	 * than SPAWN_GAP): closing should shift b (and anything transitively
	 * connected beyond it) left so the edge-to-edge gap becomes exactly
	 * SPAWN_GAP. */
	Client a = { 1, {0, 0, 100, 100}, {0} };
	Client b = { 2, {300, 0, 100, 100}, {0} };
	Client tail = { 3, {450, 0, 100, 100}, {0} };

	b.neighbor[OCT_W] = &a; a.neighbor[OCT_E] = &b;
	b.neighbor[OCT_E] = &tail; tail.neighbor[OCT_W] = &b;

	printf("Running close_gap_shifts_downstream_component...\n");
	close_gap(&a, &b);

	/* a at [0,100]; new gap should be SPAWN_GAP(20) -> b.x = 100+20 = 120. */
	CHECK(b.geom.x == 120, "b should shift left to restore exactly SPAWN_GAP");
	CHECK(tail.geom.x == 270, "tail should shift by the same amount, preserving its own spacing to b");
	CHECK(a.geom.x == 0, "a itself must not move");
	printf(b.geom.x == 120 && tail.geom.x == 270 ? "  PASS\n" : "  (see failures above)\n");
}

static void
test_close_gap_noop_if_not_connected(void)
{
	Client a = { 1, {0, 0, 100, 100}, {0} };
	Client b = { 2, {300, 0, 100, 100}, {0} };
	/* deliberately not connected */

	printf("Running close_gap_noop_if_not_connected...\n");
	resize_calls = 0;
	close_gap(&a, &b);
	CHECK(resize_calls == 0, "no resize should happen when a and b were never linked");
	CHECK(b.geom.x == 300, "b's position must be untouched");
	printf(resize_calls == 0 ? "  PASS\n" : "  (see failures above)\n");
}

static void
test_swap_middle_of_chain_transfers_third_connection(void)
{
	/* A - B - C, focus B, swap left with A (matches swap_neighbor_dir()'s
	 * own doc comment example exactly): B moves to A's old spot, A moves to
	 * B's old spot. B's old spot was adjacent to C, so C's connection should
	 * transfer from B to A (not be dropped), since A now physically occupies
	 * that spot. */
	Client A = { 1, {0,   0, 100, 100}, {0} };
	Client B = { 2, {200, 0, 100, 100}, {0} };
	Client C = { 3, {400, 0, 100, 100}, {0} };

	A.neighbor[OCT_E] = &B; B.neighbor[OCT_W] = &A;
	B.neighbor[OCT_E] = &C; C.neighbor[OCT_W] = &B;

	printf("Running swap_middle_of_chain_transfers_third_connection...\n");
	swap_neighbor_dir(&B, DIR_LEFT); /* B swaps with its West neighbor, A */

	/* Positions traded. */
	CHECK(B.geom.x == 0, "B should have moved to A's old position");
	CHECK(A.geom.x == 200, "A should have moved to B's old position");
	/* C's connection should now point at A (occupying B's old East-adjacent
	 * spot), not at B, and not be dropped. */
	CHECK(A.neighbor[OCT_E] == &C, "A should inherit the connection to C");
	CHECK(C.neighbor[OCT_W] == &A, "C's back-reference should now point at A");
	CHECK(B.neighbor[OCT_E] != &C, "B must not still claim a connection to C");
	printf(A.neighbor[OCT_E] == &C && C.neighbor[OCT_W] == &A ? "  PASS\n" : "  (see failures above)\n");
}

static void
test_swap_updates_direct_connection_between_swapped_pair(void)
{
	Client A = { 1, {0,   0, 100, 100}, {0} };
	Client B = { 2, {200, 0, 100, 100}, {0} };
	A.neighbor[OCT_E] = &B; B.neighbor[OCT_W] = &A;

	printf("Running swap_updates_direct_connection_between_swapped_pair...\n");
	swap_neighbor_dir(&B, DIR_LEFT);

	/* After trading places, A is now to B's East (B moved to x=0, A to
	 * x=200) — the direct edge between them must still exist, just flipped. */
	CHECK(B.neighbor[OCT_E] == &A, "B's East slot should now point at A");
	CHECK(A.neighbor[OCT_W] == &B, "A's West slot should now point at B");
	printf(B.neighbor[OCT_E] == &A && A.neighbor[OCT_W] == &B ? "  PASS\n" : "  (see failures above)\n");
}

static void
test_swap_preserves_off_axis_neighbors(void)
{
	/* Regression test for a real bug (found by code audit, 2026-07-09):
	 * a plus-shaped layout — C has neighbors E, W, N, S. Swap C left with
	 * W. C moves to W's old spot; C's N/S links must transfer to W (which
	 * is now standing where C used to be), not stay stale pointing at
	 * windows that are no longer actually N/S of C's new position. */
	Client C = { 1, {200, 200, 100, 100}, {0} };
	Client W = { 2, {0,   200, 100, 100}, {0} };
	Client E = { 3, {400, 200, 100, 100}, {0} };
	Client N = { 4, {200, 0,   100, 100}, {0} };
	Client S = { 5, {200, 400, 100, 100}, {0} };

	C.neighbor[OCT_W] = &W; W.neighbor[OCT_E] = &C;
	C.neighbor[OCT_E] = &E; E.neighbor[OCT_W] = &C;
	C.neighbor[OCT_N] = &N; N.neighbor[OCT_S] = &C;
	C.neighbor[OCT_S] = &S; S.neighbor[OCT_N] = &C;

	printf("Running swap_preserves_off_axis_neighbors...\n");
	swap_neighbor_dir(&C, DIR_LEFT); /* C swaps with its West neighbor, W */

	/* C and W traded positions; the E link (on the swap axis, opposite
	 * side) already has its own dedicated test above — this test is about
	 * N/S, which the old code left untouched (the bug). */
	CHECK(W.neighbor[OCT_N] == &N, "W (now at C's old spot) should inherit the North neighbor");
	CHECK(N.neighbor[OCT_S] == &W, "N's back-reference should now point at W");
	CHECK(W.neighbor[OCT_S] == &S, "W (now at C's old spot) should inherit the South neighbor");
	CHECK(S.neighbor[OCT_N] == &W, "S's back-reference should now point at W");
	CHECK(C.neighbor[OCT_N] == NULL, "C (now at W's old spot) should not still claim the North neighbor");
	CHECK(C.neighbor[OCT_S] == NULL, "C (now at W's old spot) should not still claim the South neighbor");
	printf(W.neighbor[OCT_N] == &N && N.neighbor[OCT_S] == &W
			&& W.neighbor[OCT_S] == &S && S.neighbor[OCT_N] == &W
			&& C.neighbor[OCT_N] == NULL && C.neighbor[OCT_S] == NULL
		? "  PASS\n" : "  (see failures above)\n");
}

static void
test_connect_pick_noop_if_nothing_pending(void)
{
	Client A = { 1, {0, 0, 100, 100}, {0} };
	Client *pending = NULL;

	printf("Running connect_pick_noop_if_nothing_pending...\n");
	connect_pick_complete(&pending, &A);
	CHECK(A.neighbor[OCT_N] == NULL && A.neighbor[OCT_E] == NULL
			&& A.neighbor[OCT_S] == NULL && A.neighbor[OCT_W] == NULL,
			"clicking a window with nothing armed should link nothing");
	printf("  PASS\n");
}

static void
test_connect_pick_noop_if_target_is_source(void)
{
	Client A = { 1, {0, 0, 100, 100}, {0} };
	Client *pending = &A;

	printf("Running connect_pick_noop_if_target_is_source...\n");
	connect_pick_complete(&pending, &A);
	CHECK(pending == &A, "armed source clicking itself should stay armed, not link to itself");
	CHECK(A.neighbor[OCT_N] == NULL, "a window can't connect to itself");
	printf(pending == &A ? "  PASS\n" : "  (see failures above)\n");
}

static void
test_connect_pick_completes_and_clears_pending(void)
{
	Client A = { 1, {0,   0, 100, 100}, {0} };
	Client B = { 2, {300, 0, 100, 100}, {0} };
	Client *pending = &A;

	printf("Running connect_pick_completes_and_clears_pending...\n");
	connect_pick_complete(&pending, &B);
	CHECK(pending == NULL, "completing a connect should clear the pending source");
	CHECK(A.neighbor[OCT_E] == &B, "A should now link East to B");
	CHECK(B.neighbor[OCT_W] == &A, "B should now link West back to A");
	printf(pending == NULL && A.neighbor[OCT_E] == &B && B.neighbor[OCT_W] == &A
		? "  PASS\n" : "  (see failures above)\n");
}

int
main(void)
{
	test_connect_basic_link_both_sides();
	test_connect_noop_if_slot_occupied();
	test_connect_no_double_link_when_already_connected();
	test_sever_clears_both_sides();
	test_close_gap_shifts_downstream_component();
	test_close_gap_noop_if_not_connected();
	test_swap_middle_of_chain_transfers_third_connection();
	test_swap_updates_direct_connection_between_swapped_pair();
	test_swap_preserves_off_axis_neighbors();
	test_connect_pick_noop_if_nothing_pending();
	test_connect_pick_noop_if_target_is_source();
	test_connect_pick_completes_and_clears_pending();

	printf("\n===================================\n");
	printf("Total assertion failures: %d\n", failures);
	printf("===================================\n");
	return failures ? 1 : 0;
}
