/* Rail — the 1D scrolling order keyboard-spawned managed windows flow into
 * (the "hybrid rail on the free canvas", layout rethink Phase 2 —
 * obsidian/plan/layout-rethink.md, obsidian/implementation/rail.md).
 *
 * A doubly-linked order over rail_prev/rail_next on Client, anchored by the
 * global rail_head (leftmost member). This replaces the removed 8-octant
 * connection graph with one dimension and one default rail: a window whose
 * rail_prev == rail_next == NULL is *not* on the rail (free/float/overlay
 * bucket) and keeps its own free position.
 *
 * Separately-compiled TU (like directional_focus.c): links against dwl.c's
 * externed globals (clients, selmon, rail_head) and functions (focustop,
 * resize, client_set_target_geom, viewport_follow_focus) via kalin.h. The
 * placement of a *newly mapped* window is done inline in dwl.c's mapnotify()
 * (it owns the snap_grid/resize sequence and the parent lookup); this file owns
 * the linkage bookkeeping (insert/remove), the successor-shift that keeps the
 * row gap-free, and the two bound actions (1D swap + focus-next/prev). */
#include "kalin.h"

/* Shift every rail successor of `from` (exclusive) by `dx` in world x, gliding
 * via the spring so the row slides instead of snapping — the same feel the swap
 * and the old group-drag used. A forward walk over rail_next, niri-style: used
 * both to open a gap for an inserted window (dx > 0) and to close the gap a
 * closed window left (dx < 0). NULL-safe on a dangling chain. */
static void
rail_shift_successors(Client *from, int dx)
{
	Client *s;

	if (!from || dx == 0)
		return;

	for (s = from->rail_next; s; s = s->rail_next) {
		struct wlr_box g = s->geom;
		g.x += dx;
		client_set_target_geom(s, g);
	}
}

/* Splice `c` into the rail immediately after `p` (or make it the sole rail /
 * new tail when `p` is off-rail or NULL). Pure linkage — the caller has already
 * positioned `c`; the successor-shift that opens room for it is done separately
 * (mapnotify does it after resize()) so this stays reusable for restore too. */
void
rail_insert_after(Client *p, Client *c)
{
	if (!c)
		return;

	/* Anchor resolution. The common case is p already on the rail — insert
	 * right after it. Edge cases keep the *one default rail* a single chain: */
	if (!p) {
		/* No parent at all (very first window): c seeds the rail. */
		if (!rail_head)
			rail_head = c;
		else
			for (p = rail_head; p->rail_next; p = p->rail_next)
				; /* fall through to splice after the tail */
		if (!p) {
			c->rail_prev = c->rail_next = NULL;
			return;
		}
	} else if (!p->rail_prev && !p->rail_next && rail_head != p) {
		/* p exists but isn't on the rail yet. If there's no rail, p seeds it as
		 * head and c follows — so the first keyboard spawn and its parent both
		 * join, in spawn order. If a rail already exists elsewhere, don't fork
		 * it: append c to the tail so the single chain grows. */
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

/* Unlink `c` from the rail (fixing rail_head if it was the head) and close the
 * gap it leaves by shifting its successors left by its width + gap. No-op for a
 * window that was never on the rail. Null the pointers — a dangling rail_next
 * after a close is the classic use-after-free, matching how unmapnotify() nulls
 * the other per-client refs. */
void
rail_remove(Client *c)
{
	Client *prev, *next;

	if (!c || (!c->rail_prev && !c->rail_next && rail_head != c))
		return; /* not on the rail */

	prev = c->rail_prev;
	next = c->rail_next;

	/* Close the gap: shift c and everything past it left by c's footprint, so
	 * the successors slide into where c was (niri-style). Walk from prev when
	 * there is one (rail_shift_successors starts at ->rail_next), else the
	 * successors start at the head. */
	if (next) {
		int dx = -(c->geom.width + SPAWN_GAP);
		Client *s;
		for (s = next; s; s = s->rail_next) {
			struct wlr_box g = s->geom;
			g.x += dx;
			client_set_target_geom(s, g);
		}
	}

	if (prev)
		prev->rail_next = next;
	if (next)
		next->rail_prev = prev;
	if (rail_head == c)
		rail_head = next; /* new leftmost (NULL if the rail is now empty) */

	c->rail_prev = c->rail_next = NULL;
}

/* Super+Ctrl+Left/Right: swap the focused window with its rail neighbour in that
 * direction — trading both screen position (animated) and rail linkage. No-op at
 * the ends and for Up/Down (the rail is 1D). Mirrors the removed
 * swap_neighbor_dir() but over the single-axis rail order. */
void
rail_swap_dir(const Arg *arg)
{
	Client *c, *n;
	int cx, cy;

	if (!selmon)
		return;
	c = focustop(selmon);
	if (!c)
		return;

	switch (arg->i) {
	case DIR_LEFT:  n = c->rail_prev; break;
	case DIR_RIGHT: n = c->rail_next; break;
	default: return; /* Up/Down: the rail has no vertical order */
	}
	if (!n)
		return; /* at the end of the rail */

	/* Trade positions (not sizes) — each window keeps its own size and glides
	 * to the other's old spot via the spring, same as the old graph swap. */
	cx = c->geom.x; cy = c->geom.y;
	client_set_target_geom(c, (struct wlr_box){
		.x = n->geom.x, .y = n->geom.y,
		.width = c->geom.width, .height = c->geom.height});
	client_set_target_geom(n, (struct wlr_box){
		.x = cx, .y = cy,
		.width = n->geom.width, .height = n->geom.height});

	/* Swap the linkage so the rail order tracks the on-screen order: c and n
	 * exchange their slots in the chain. Handle the adjacent-pair case (n is
	 * directly next to c) without aliasing the outer neighbours. */
	{
		Client *a_prev = c->rail_prev, *a_next = c->rail_next;
		Client *b_prev = n->rail_prev, *b_next = n->rail_next;

		if (a_next == n) {           /* c ... n : c then n */
			c->rail_prev = n; c->rail_next = b_next;
			n->rail_prev = a_prev; n->rail_next = c;
			if (a_prev) a_prev->rail_next = n;
			if (b_next) b_next->rail_prev = c;
		} else if (b_next == c) {    /* n ... c : n then c */
			n->rail_prev = c; n->rail_next = a_next;
			c->rail_prev = b_prev; c->rail_next = n;
			if (b_prev) b_prev->rail_next = c;
			if (a_next) a_next->rail_prev = n;
		} else {                     /* non-adjacent (defensive; can't happen
		                              * for prev/next, but keeps the swap total) */
			c->rail_prev = b_prev; c->rail_next = b_next;
			n->rail_prev = a_prev; n->rail_next = a_next;
			if (b_prev) b_prev->rail_next = c;
			if (b_next) b_next->rail_prev = c;
			if (a_prev) a_prev->rail_next = n;
			if (a_next) a_next->rail_prev = n;
		}

		if (rail_head == c)
			rail_head = n;
		else if (rail_head == n)
			rail_head = c;
	}

	/* Keep the focused (moved) window framed, like the old swap did. */
	viewport_follow_focus();

	/* Persist the new rail order + the two swapped positions right away, so a
	 * restart restores the reordering (the rail predecessor of each is now
	 * different — layout Phase 6). Without this a swap is lost on the next
	 * boot, since no geometry-change save otherwise fires for it. */
	persistence_save();
}

/* Discrete rail scroll: focus the rail neighbour in the pressed direction and
 * frame it with an explicit camera jump (viewport_center_on), the "snap to the
 * next/prev rail window" companion to the loose free pan. Left/Right walk the
 * rail; Up/Down are no-ops (1D). Only meaningful when the focused window is on
 * the rail. */
void
rail_focus_dir(const Arg *arg)
{
	Client *c, *n;

	if (!selmon)
		return;
	c = focustop(selmon);
	if (!c)
		return;

	switch (arg->i) {
	case DIR_LEFT:  n = c->rail_prev; break;
	case DIR_RIGHT: n = c->rail_next; break;
	default: return;
	}
	if (!n)
		return;

	focusclient(n, 1);
	viewport_center_on(n);
}

/* Open the gap for a freshly-inserted rail member: shift its successors right by
 * its footprint. Exposed to mapnotify() via the header's rail_insert_after()
 * doc; kept here so the forward-walk lives with the rest of the rail logic. */
void
rail_open_gap_after(Client *c)
{
	if (!c)
		return;
	rail_shift_successors(c, c->geom.width + SPAWN_GAP);
}

/* A rail member grew (wider buffer, or an explicit fit/resize): push its
 * successors right so the growth doesn't cover them — the 1D forward-walk heir
 * of the old graph's resolve_growth_overlap() (layout rethink Phase 3,
 * obsidian/plan/layout-impl.md). The push amount is the *overlap* of `grown`'s
 * right edge (plus the row gap) into its immediate successor's left edge; the
 * whole successor chain slides by that one delta (via rail_shift_successors),
 * so the inter-successor spacing the rail already maintains is preserved and
 * only what's actually covered moves. Position-based, not delta-based, so it's
 * correct whether the caller grew top-left-anchored (resizefocused) or
 * centered (fitwidth), and idempotent: once the successors clear, the overlap
 * is <= 0 and this is a no-op — which is also the feedback-loop guard, since
 * commitnotify() calls this on every committed buffer.
 *
 * allow_overlap re-home (Phase 3): a flagged window grows *over* its successors
 * instead of pushing them, so this is a no-op for it — the flag's live meaning
 * under the rail (Super+Shift+o). No-op too for an off-rail window. */
void
rail_push_growth(Client *grown)
{
	Client *first;
	int overlap;

	if (!grown || grown->allow_overlap)
		return;
	if (!grown->rail_prev && !grown->rail_next && rail_head != grown)
		return; /* not on the rail */

	first = grown->rail_next;
	if (!first)
		return; /* nothing to the right to push */

	overlap = (grown->geom.x + grown->geom.width + SPAWN_GAP) - first->geom.x;
	if (overlap > 0)
		rail_shift_successors(grown, overlap);
}
