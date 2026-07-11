/* Standalone logic test for resolve_growth_overlap() (dwl.c) — the fix for a
 * window that grows *after* it was already placed (e.g. an Electron/GTK app
 * settling into a bigger self-requested size on a commit after map) pushing
 * whatever connection-graph neighbor was positioned assuming the smaller
 * size, instead of silently overlapping it.
 *
 * dwl.c is a monolithic compositor binary that can't be linked standalone
 * (it needs a running wlroots backend), so this test reimplements the exact
 * geometry/graph math verbatim (opposite_octant, collect_component,
 * resolve_growth_overlap) against a minimal Client stand-in, with resize()
 * stubbed to just record the new geometry — the same technique already used
 * for this repo's other pure-logic unit tests (test_client_lifecycle.c,
 * test_binds.c). Keep this in sync with dwl.c if that logic changes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define SPAWN_GAP 20
enum Octant { OCT_N, OCT_NE, OCT_E, OCT_SE, OCT_S, OCT_SW, OCT_W, OCT_NW };

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

static int
opposite_octant(int oct)
{
	return (oct + 4) % 8;
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

static void
resolve_growth_overlap(Client *c)
{
	static const int dirs[4] = { OCT_E, OCT_S, OCT_W, OCT_N };
	int i;

	for (i = 0; i < 4; i++) {
		int oct = dirs[i];
		Client *n = c->neighbor[oct];
		int opp, overlap, dx = 0, dy = 0, ncomp, k;
		Client *component[256];

		if (!n)
			continue;

		switch (oct) {
		case OCT_E:
			overlap = (int)(c->geom.x + c->geom.width + SPAWN_GAP) - n->geom.x;
			dx = overlap;
			break;
		case OCT_W:
			overlap = (n->geom.x + n->geom.width + SPAWN_GAP) - (int)c->geom.x;
			dx = -overlap;
			break;
		case OCT_S:
			overlap = (int)(c->geom.y + c->geom.height + SPAWN_GAP) - n->geom.y;
			dy = overlap;
			break;
		case OCT_N:
			overlap = (n->geom.y + n->geom.height + SPAWN_GAP) - (int)c->geom.y;
			dy = -overlap;
			break;
		default:
			continue;
		}

		if (overlap <= 0)
			continue;

		opp = opposite_octant(oct);
		n->neighbor[opp] = NULL;
		ncomp = collect_component(n, component, (int)(sizeof(component) / sizeof(component[0])));
		n->neighbor[opp] = c;

		for (k = 0; k < ncomp; k++) {
			struct wlr_box nb = component[k]->geom;
			nb.x += dx;
			nb.y += dy;
			resize(component[k], nb, 0);
		}
	}
}

static int failures = 0;

#define CHECK(cond, msg) do { \
	if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
} while (0)

static void
test_chain_grow_middle_pushes_tail(void)
{
	/* 1 - 2 - 3, matching the reported foot/foot/foot + big-app chain:
	 * client 2 grows (e.g. a settling Electron app) into client 3.
	 * Client 1 (upstream, unaffected direction) must not move; client 3
	 * (downstream, in the way) must shift right by exactly the overlap. */
	Client c1 = { 1, {0,   0, 300, 200}, {0} };
	Client c2 = { 2, {320, 0, 300, 200}, {0} };
	Client c3 = { 3, {640, 0, 300, 200}, {0} };

	c1.neighbor[OCT_E] = &c2; c2.neighbor[OCT_W] = &c1;
	c2.neighbor[OCT_E] = &c3; c3.neighbor[OCT_W] = &c2;

	printf("Running chain_grow_middle_pushes_tail...\n");

	/* c2 settles into a much wider size after map: 300 -> 500. */
	c2.geom.width = 500;
	resolve_growth_overlap(&c2);

	/* c2's new right edge (320+500=820) + GAP(20) = 840, c3 was at 640:
	 * overlap = 840-640 = 200, so c3 must land at 840. */
	CHECK(c3.geom.x == 840, "client3 should shift right by the overlap amount");
	CHECK(c3.geom.y == 0, "client3's y must be untouched by a pure-width grow");
	CHECK(c1.geom.x == 0, "client1 (upstream) must not move");
	CHECK(c2.geom.x == 320, "client2 itself (the one that grew) must not be re-shifted");

	printf(c3.geom.x == 840 && c1.geom.x == 0 ? "  PASS\n" : "  (see failures above)\n");
}

static void
test_no_overlap_is_a_noop(void)
{
	Client a = { 1, {0, 0, 100, 100}, {0} };
	Client b = { 2, {200, 0, 100, 100}, {0} };
	a.neighbor[OCT_E] = &b; b.neighbor[OCT_W] = &a;

	printf("Running no_overlap_is_a_noop...\n");
	resize_calls = 0;
	resolve_growth_overlap(&a);
	CHECK(resize_calls == 0, "no shift should happen when there's no overlap");
	CHECK(b.geom.x == 200, "neighbor position must be unchanged");
	printf(resize_calls == 0 ? "  PASS\n" : "  (see failures above)\n");
}

static void
test_downstream_component_moves_together(void)
{
	/* 1 - 2 - 3 - 4: growing client1 into client2 must drag the whole
	 * downstream tail (2,3,4) together, preserving their relative spacing,
	 * not just the immediate neighbor. */
	Client c1 = { 1, {0,   0, 300, 200}, {0} };
	Client c2 = { 2, {320, 0, 300, 200}, {0} };
	Client c3 = { 3, {640, 0, 300, 200}, {0} };
	Client c4 = { 4, {960, 0, 300, 200}, {0} };

	c1.neighbor[OCT_E] = &c2; c2.neighbor[OCT_W] = &c1;
	c2.neighbor[OCT_E] = &c3; c3.neighbor[OCT_W] = &c2;
	c3.neighbor[OCT_E] = &c4; c4.neighbor[OCT_W] = &c3;

	printf("Running downstream_component_moves_together...\n");

	c1.geom.width = 400; /* grows from 300 to 400: 100px overlap into c2 */
	resolve_growth_overlap(&c1);

	CHECK(c2.geom.x == 420, "immediate neighbor shifts by the overlap");
	CHECK(c3.geom.x == 740, "downstream chain member shifts by the same amount");
	CHECK(c4.geom.x == 1060, "far downstream chain member shifts by the same amount");
	printf(c2.geom.x == 420 && c3.geom.x == 740 && c4.geom.x == 1060 ? "  PASS\n" : "  (see failures above)\n");
}

int
main(void)
{
	test_chain_grow_middle_pushes_tail();
	test_no_overlap_is_a_noop();
	test_downstream_component_moves_together();

	printf("\n===================================\n");
	printf("Total assertion failures: %d\n", failures);
	printf("===================================\n");
	return failures ? 1 : 0;
}
