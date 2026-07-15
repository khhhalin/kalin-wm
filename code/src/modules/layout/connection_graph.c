/* Connection-graph: kalin-wm's spawn-adjacency model. Every window is free-
 * positioned; up to 8 directional neighbor slots per window (enum Octant,
 * kalin.h) record which window is to its N/NE/E/SE/S/SW/W/NW. This is what
 * replaced the old column-layout/anchored-window tiling (see [[ledger]]) —
 * connections are established at spawn time (mapnotify, dwl.c) and maintained
 * through moves, swaps, resizes, and closes by the functions below.
 *
 * Separately-compiled TU: links against dwl.c's externed globals (clients,
 * selmon, viewport) and functions (focustop, resize, client_set_target_geom,
 * status_mark_dirty, persistence_save, viewport_follow_focus) via kalin.h.
 * connect_clients()/resolve_growth_overlap()/sever_connection() are the
 * cross-module public API (also called from modules/ipc.c and
 * modules/input/resize_actions.c); everything else here is only called from
 * dwl.c. */
#include "kalin.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Anchor point on rect (rx,ry,rw,rh)'s boundary closest to the other rect's
 * center — mirrors quickshell's LineGeometry._edgeAnchor()
 * (~/environment/quickshell/modules/services/LineGeometry.qml) so the
 * compositor's click hit-test below lines up with the rendered sparkle line. */
static void
edge_anchor(float rx, float ry, float rw, float rh,
		float ox, float oy, float ow, float oh, float *ax, float *ay)
{
	float cx = rx + rw / 2.0f, cy = ry + rh / 2.0f;
	float ocx = ox + ow / 2.0f, ocy = oy + oh / 2.0f;
	float dx = ocx - cx, dy = ocy - cy;
	if (fabsf(dx) > fabsf(dy)) {
		*ax = dx > 0 ? rx + rw : rx;
		*ay = cy;
	} else {
		*ax = cx;
		*ay = dy > 0 ? ry + rh : ry;
	}
}

/* octant and its opposite are always 4 apart (mod 8) — see enum Octant. Also
 * called directly from dwl.c (unmapnotify's splice logic), so not static. */
int
opposite_octant(int oct)
{
	return (oct + 4) % 8;
}

/* Which octant (see enum Octant) the direction from (fx,fy) to (tx,ty) falls
 * into, snapped to the nearest of 8 compass points. */
static int
octant_from_delta(float dx, float dy)
{
	static const float step = (float)M_PI / 4.0f; /* 45 degrees per octant */
	float angle = atan2f(dx, -dy); /* 0 = north, increasing clockwise */
	int oct;
	if (angle < 0)
		angle += 2.0f * (float)M_PI;
	oct = (int)lroundf(angle / step) % 8;
	return oct;
}

/* Whether a and b are already connected on any octant (not just the one
 * octant_from_delta() would currently compute for their geometry — a and b
 * may have moved since they were linked, so the *stored* edge can be on a
 * different octant than their present angle would suggest). */
static int
clients_already_linked(Client *a, Client *b)
{
	int i;
	for (i = 0; i < 8; i++)
		if (a->neighbor[i] == b)
			return 1;
	return 0;
}

/* Connect two clients in the direction the angle between their centers
 * dictates, symmetrically (a->neighbor[oct] = b, b->neighbor[opposite] = a).
 * No-ops if either endpoint's relevant slot is already occupied — a window
 * only ever has one connection per compass direction — or if a and b are
 * already linked on some other octant (without this check, a caller that
 * reconnects a pair whose relative angle changed since they were first
 * linked, e.g. after a swap or gap-close shifted one of them diagonally,
 * could silently double-link them across two slots instead of leaving the
 * existing edge alone). */
void
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

/* BFS over the connection graph (all 8 neighbor[] slots) starting from
 * `start`, collecting every reachable client into `out` (capacity `max`).
 * Used by motionnotify()'s CurMove handling so dragging any window drags its
 * whole connected component together, and by resolve_growth_overlap()/
 * close_gap() below. A node can be reachable via more than one path (e.g. a
 * diamond of 4 mutually-connected windows), so membership is checked before
 * adding. */
int
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
	/* A component larger than `max` silently drops whatever didn't fit —
	 * the caller's shift/drag loop then only moves the clients that DID
	 * fit, leaving the rest of the chain visibly behind with no error
	 * anywhere. Every caller passes a 256-slot array and any daily-use
	 * layout is nowhere near that, so this is very unlikely to actually
	 * fire — but if it ever does, it should say so instead of just
	 * producing a layout that looks broken for no apparent reason. */
	if (count == max)
		wlr_log(WLR_ERROR, "collect_component: component has more than %d "
				"clients, some were not collected (dropped from this move/shift)", max);
	return count;
}

/* A client that grows *after* it was already placed (e.g. spawn-placement or
 * insert-shift sized neighbors around it based on its geometry at map time,
 * then an Electron/GTK app like Obsidian settles into a much bigger
 * self-requested size on a later commit) can end up overlapping whichever
 * neighbor was positioned assuming the smaller size — nothing previously
 * re-ran the shift once that happened. Called after commitnotify() applies a
 * grown c->geom: for each of the 4 primary directions, if c's new edge would
 * now overlap that neighbor, push the neighbor (and everything still
 * transitively connected beyond it) further away by the overlap amount,
 * mirroring the same collect_component()-based shift mapnotify()'s
 * insert-between-neighbors path already uses. */
void
resolve_growth_overlap(Client *c)
{
	static const int dirs[4] = { OCT_E, OCT_S, OCT_W, OCT_N };
	int i;

	if (c->allow_overlap)
		return;

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

		/* Exclude c's own side of the connection while collecting n's
		 * component, so c itself (which isn't moving) isn't shifted too. */
		opp = opposite_octant(oct);
		n->neighbor[opp] = NULL;
		ncomp = collect_component(n, component, (int)LENGTH(component));
		n->neighbor[opp] = c;

		for (k = 0; k < ncomp; k++) {
			struct wlr_box nb = component[k]->geom;
			nb.x += dx;
			nb.y += dy;
			resize(component[k], nb, 0);
		}
	}
}

/* After splicing a<->b together (see unmapnotify()'s connection-graph
 * cleanup — a and b were both connected to a client that's now closing, and
 * just got spliced directly to each other), close whatever gap that
 * removal left between them: shift b, and everything still transitively
 * connected beyond it, so the facing edge-to-edge distance between a and b
 * becomes exactly SPAWN_GAP instead of whatever the removed client's
 * footprint used to occupy. a doesn't move — b's whole side of the line
 * slides in to meet it, the same "shift a whole downstream component"
 * technique collect_component()-based shifts elsewhere use (spawn-insert,
 * resolve_growth_overlap()). No-ops if connect_clients() didn't actually
 * manage to link a<->b (e.g. a real slot conflict), since there's then no
 * single well-defined axis to close along. */
void
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
		return; /* connect_clients() didn't end up linking them */

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

	/* Exclude a's side while collecting b's component, so a (which isn't
	 * moving) doesn't get dragged along too. */
	b->neighbor[opp] = NULL;
	ncomp = collect_component(b, component, (int)LENGTH(component));
	b->neighbor[opp] = a;

	for (k = 0; k < ncomp; k++) {
		struct wlr_box nb = component[k]->geom;
		nb.x += shift_x;
		nb.y += shift_y;
		resize(component[k], nb, 0);
	}
}

/* Sever the connection between these two specific clients, if one exists
 * (clears whichever slot(s) reference each other; normally exactly one on
 * each side). Shared by the IPC "sever <a> <b>" command and
 * connection_click_hit()'s caller in buttonpress(), so both apply the cut
 * identically. */
void
sever_connection(uint32_t id_a, uint32_t id_b)
{
	Client *a = NULL, *b = NULL, *c;
	int i;

	if (!id_a || !id_b)
		return;
	wl_list_for_each(c, &clients, link) {
		if (c->id == id_a) a = c;
		if (c->id == id_b) b = c;
	}
	if (!a || !b)
		return;
	for (i = 0; i < 8; i++) {
		if (a->neighbor[i] == b)
			a->neighbor[i] = NULL;
		if (b->neighbor[i] == a)
			b->neighbor[i] = NULL;
	}
	status_mark_dirty();
}

/* Hit-test a screen-space point against every live connection line and
 * report the nearest one's two endpoint ids if within CONN_HIT_RADIUS_PX.
 * Returns 1 and fills out_a and out_b if a line was hit, else returns 0.
 *
 * This — not quickshell — is what actually detects a click-to-sever: while
 * testing ConnectionLines.qml's line-shaped input mask (a partial
 * wlr-layer-shell input region, well short of the whole output), clicks
 * never reached it at all on this wlroots/Quickshell combination, even
 * though an exact full-output-sized region worked every time. A full-output
 * mask isn't viable here since it would swallow every Super-held click
 * anywhere on screen, including normal window drags (Wayland input has no
 * "fall through if unhandled" — a client's own accepting input region is
 * final). Rather than chase that further, the compositor does the hit-test
 * itself with the same graph and camera transform it already has for the
 * IPC broadcast; quickshell's overlay only needs to *draw* the line. */
#define CONN_HIT_RADIUS_PX 20.0f
int
connection_click_hit(double sx, double sy, uint32_t *out_a, uint32_t *out_b)
{
	Client *c;
	uint32_t best_a = 0, best_b = 0;
	float best_dist = CONN_HIT_RADIUS_PX;
	int i;

	wl_list_for_each(c, &clients, link) {
		for (i = 0; i < 8; i++) {
			Client *n = c->neighbor[i];
			float crx, cry, crw, crh, nrx, nry, nrw, nrh;
			float ax1, ay1, ax2, ay2, dx, dy, len2, t, ccx, ccy, dist;
			/* Each edge is stored on both endpoints; only test it from the
			 * lower-id side so it isn't checked (harmlessly) twice. */
			if (!n || n->id < c->id)
				continue;

			crx = WORLD_TO_SCREEN_X(c->mon, c->geom.x); cry = WORLD_TO_SCREEN_Y(c->mon, c->geom.y);
			crw = c->geom.width * MON_ZOOM_SAFE(c->mon); crh = c->geom.height * MON_ZOOM_SAFE(c->mon);
			nrx = WORLD_TO_SCREEN_X(n->mon, n->geom.x); nry = WORLD_TO_SCREEN_Y(n->mon, n->geom.y);
			nrw = n->geom.width * MON_ZOOM_SAFE(n->mon); nrh = n->geom.height * MON_ZOOM_SAFE(n->mon);

			edge_anchor(crx, cry, crw, crh, nrx, nry, nrw, nrh, &ax1, &ay1);
			edge_anchor(nrx, nry, nrw, nrh, crx, cry, crw, crh, &ax2, &ay2);

			dx = ax2 - ax1; dy = ay2 - ay1;
			len2 = dx * dx + dy * dy;
			if (len2 < 1.0f) {
				ccx = ax1; ccy = ay1;
			} else {
				t = ((float)sx - ax1) * dx + ((float)sy - ay1) * dy;
				t /= len2;
				if (t < 0) t = 0;
				if (t > 1) t = 1;
				ccx = ax1 + dx * t;
				ccy = ay1 + dy * t;
			}
			dist = hypotf((float)sx - ccx, (float)sy - ccy);
			if (dist <= best_dist) {
				best_dist = dist;
				best_a = c->id;
				best_b = n->id;
			}
		}
	}
	if (!best_a || !best_b)
		return 0;
	*out_a = best_a;
	*out_b = best_b;
	return 1;
}

/* Menu-armed manual connect: Super+L (WindowActions.qml's "Link" button)
 * arms the currently focused window as a pending source; the next left-click
 * on a *different* window (buttonpress(), dwl.c) completes the connection.
 * Exists because connections otherwise only ever form automatically (spawn-
 * adjacency, splice-on-insert/close) — there was no way to link two windows
 * that didn't happen to spawn next to each other. A file-static pointer (not
 * an id) is fine here: it's only ever read/cleared within the same short
 * press-to-click window, well before the pointed-to client could be freed
 * (any close path already calls connect_pick_cancel() via the same
 * unmapnotify() cleanup that clears neighbor[] — see below). */
static Client *pending_connect_source;

void
connect_pick_arm(void)
{
	if (!selmon)
		return;
	pending_connect_source = focustop(selmon);
	status_mark_dirty();
}

void
connect_pick_cancel(void)
{
	if (!pending_connect_source)
		return;
	pending_connect_source = NULL;
	status_mark_dirty();
}

void
connect_pick_complete(Client *target)
{
	if (!pending_connect_source || !target || target == pending_connect_source)
		return;
	/* connect_clients() already no-ops on an occupied slot or an existing
	 * link between the pair, so no extra guard is needed here. */
	connect_clients(pending_connect_source, target);
	connect_pick_cancel();
}

Client *
connect_pick_pending(void)
{
	return pending_connect_source;
}

/* Swap the focused window with whichever window occupies its connection
 * graph's neighbor slot in this direction (no-op if that slot is empty —
 * unlike focus_directional(), this never falls back to a geometric nearest-
 * neighbor search; it only acts on a real, already-established connection). */
void
swap_neighbor_dir(const Arg *arg)
{
	Client *c, *n;
	int oct, opp;
	int cx, cy;

	if (!selmon)
		return;
	c = focustop(selmon);
	if (!c)
		return;

	switch (arg->i) {
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

	/* Trade positions (not sizes) — each window keeps its own size, and
	 * glides to the other's old spot via the same spring animation
	 * group-drag uses, for the same "fun tether" feel established
	 * elsewhere this session. */
	cx = c->geom.x; cy = c->geom.y;
	client_set_target_geom(c, (struct wlr_box){
		.x = n->geom.x, .y = n->geom.y,
		.width = c->geom.width, .height = c->geom.height});
	client_set_target_geom(n, (struct wlr_box){
		.x = cx, .y = cy,
		.width = n->geom.width, .height = n->geom.height});

	/* They've traded places, so which side of each other they're now on has
	 * flipped too — update the slot assignment to match, or the next
	 * Super+Ctrl+Arrow press on either would try to swap the same pair back
	 * through what's now the wrong direction. */
	c->neighbor[oct] = NULL;
	n->neighbor[opp] = NULL;

	/* The slot each is about to claim (c's opp, n's oct) might already hold
	 * a third window — e.g. swapping the middle of a 3-window chain (A-B-C,
	 * focus B, swap left with A) moves B to A's old spot and A to B's old
	 * spot. B's old spot was adjacent to C, and A has just moved into it —
	 * so A is now physically where that adjacency to C lives. Transfer C's
	 * connection from B to A (rather than dropping it) so the whole chain's
	 * lines stay intact, just re-anchored to whichever of the swapped pair
	 * now actually occupies that spot. Only transferred if the recipient's
	 * own slot there is free; a genuine three-way clash still just drops
	 * the third connection (rare, and no worse than before). */
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

	/* c and n traded *positions* exactly (own sizes kept) — so any neighbor
	 * they had in a direction other than the swap axis (oct/opp, handled
	 * above) is still standing exactly where it always was, and is now
	 * adjacent to whichever of the pair moved into that spot. Without this,
	 * those off-axis links kept pointing at the pre-swap layout: stale
	 * neighbor slots that sent directional-focus/another swap through them
	 * to the wrong window, and drew connection lines that contradicted the
	 * actual on-screen layout. Since c and n swap the *same* slot index i on
	 * both sides, there's no collision to worry about the way the oct/opp
	 * transfer above has to (that one can land in an already-occupied
	 * different slot) — a plain swap of index i is always safe. */
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
				continue; /* both empty, or the same third client either way */
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

	status_mark_dirty();
	persistence_save();

	/* c is still the focused client (nothing called focusclient()), so
	 * focusclient()'s own camera-follow never fires for this move — without
	 * this, the camera silently stayed put while the window it's supposed
	 * to be following glided away underneath it. */
	viewport_follow_focus();
}
