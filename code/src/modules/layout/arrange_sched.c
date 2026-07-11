/* Arrangement scheduler: coalesces arrange()/printstatus() calls.
 *
 * Separately-compiled TU: links against dwl.c's externed `mons`, `event_loop`,
 * `arrange`, `printstatus` via kalin.h.
 *
 * Every mutation that might affect layout or published state (window spawn,
 * focus change, floating/fullscreen toggle, monitor hotplug, camera settle,
 * crop, minimize, ...) used to call arrange(Monitor*) and/or printstatus()
 * directly — each a full, unconditional O(clients) sweep (arrange() also runs
 * arrange_columns(), a from-scratch rebuild of column assignment). A single
 * logical event like a window spawn can touch several of these call sites in
 * one synchronous chain (applyrules -> setmon -> setfullscreen -> setfloating),
 * so the sweep ran multiple times for one real change. The only thing
 * preventing that from compounding further was a hand-added equality check
 * inside a handful of the individual functions — easy to get right once, easy
 * to miss at the next new call site, since nothing stops a redundant call
 * structurally.
 *
 * This module inverts that: call sites mark a monitor "needs arranging" (or
 * global state "needs publishing") instead of doing the work themselves. The
 * first such call in an event-loop iteration schedules a `wl_event_loop_add_idle`
 * callback — the standard wlroots idiom for "run once, right after the current
 * batch of synchronous event handling, before the loop blocks for the next
 * event." Any number of mark_dirty calls within that same iteration collapse
 * into exactly one real arrange() per dirty monitor and one real printstatus(),
 * regardless of how many call sites triggered it or in what order. */
#include "kalin.h"

static int status_dirty;
static int flush_scheduled;

static void
arrange_flush_idle(void *data)
{
	Monitor *m;
	int did_arrange = 0;
	(void)data;

	flush_scheduled = 0;

	wl_list_for_each(m, &mons, link) {
		if (!m->arrange_dirty)
			continue;
		m->arrange_dirty = 0;
		arrange(m);
		did_arrange = 1;
	}

	if (status_dirty || did_arrange) {
		status_dirty = 0;
		printstatus();
	}
}

static void
arrange_schedule_flush(void)
{
	if (flush_scheduled || !event_loop)
		return;
	flush_scheduled = 1;
	wl_event_loop_add_idle(event_loop, arrange_flush_idle, NULL);
}

void
arrange_mark_dirty(Monitor *m)
{
	if (!m)
		return;
	m->arrange_dirty = 1;
	arrange_schedule_flush();
}

void
status_mark_dirty(void)
{
	status_dirty = 1;
	arrange_schedule_flush();
}
