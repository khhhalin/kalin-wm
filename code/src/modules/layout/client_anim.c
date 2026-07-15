/* Window spring-glide animation: client_set_target_geom() records a client's
 * target *world* geometry; clients_anim_step() (run each frame from
 * rendermon(), so it is vsync-aligned with the camera) springs the rendered
 * world position toward it. Per frame this only moves the client's scene
 * node — cheap; a full resize() runs once on settle.
 *
 * Separately-compiled TU: links against dwl.c's externed globals (clients)
 * and functions (resize) via kalin.h, and anim_stiffness/anim_damping from
 * config.h (the same tuning constants dwl.c itself uses — not otherwise
 * shared via kalin.h, so included directly here; each TU that needs them
 * gets its own copy, same as every other static const in config.h). Both
 * clients_anim_step() and client_set_target_geom() are public API other
 * modules call too (connection_graph.c's swap_neighbor_dir(),
 * modules/viewport code) — declared in kalin.h. */
#include "kalin.h"
#include "config.h"

static struct timespec client_anim_last;
static int client_anim_have_last;
static float client_anim_accum;

/* Fixed physics step: rendermon() can be invoked far faster than any real
 * display refresh on some backends (confirmed via logging: thousands of
 * calls/sec after a burst of activity, vs. an expected ~60-144). Stepping the
 * spring with the *actual* per-call dt in that regime makes each step
 * vanishingly small (force*dt underflows toward nothing), so the animation
 * makes near-zero real progress while still doing full per-client scene work
 * and re-requesting a frame every call — a self-sustaining busy loop that
 * looks like a freeze. A fixed-timestep accumulator decouples "how much
 * physics happens" from "how often we're called": real elapsed time banks up
 * here, and a step (the expensive part) only runs once enough has
 * accumulated, so a burst of rapid calls collapses into cheap no-ops between
 * steps instead of thousands of degenerate ones. Total simulated time still
 * matches real elapsed time exactly — this doesn't change animation speed. */
#define ANIM_FIXED_DT (1.0f / 120.0f)

static float
spring_step(float cur, float target, float *vel, float dt)
{
	/* Semi-implicit spring: x'' = -k(x-target) - c*x'. */
	float force = -anim_stiffness * (cur - target) - anim_damping * (*vel);
	*vel += force * dt;
	return cur + (*vel) * dt;
}

/* Advance every animating client by as many fixed physics steps as real time
 * has accumulated. Returns 1 while any client is still moving so rendermon()
 * keeps scheduling frames. */
int
clients_anim_step(void)
{
	struct timespec now;
	Client *c;
	float elapsed;
	int still = 0;

	clock_gettime(CLOCK_MONOTONIC, &now);
	if (!client_anim_have_last) {
		elapsed = ANIM_FIXED_DT;
		client_anim_have_last = 1;
		client_anim_accum = 0.0f;
	} else {
		elapsed = (float)(now.tv_sec - client_anim_last.tv_sec)
			+ (float)(now.tv_nsec - client_anim_last.tv_nsec) / 1e9f;
	}
	client_anim_last = now;
	if (elapsed <= 0.0f)
		elapsed = ANIM_FIXED_DT;
	if (elapsed > 0.1f)
		elapsed = 0.1f; /* clamp after a stall so windows don't jump */
	client_anim_accum += elapsed;

	while (client_anim_accum >= ANIM_FIXED_DT) {
		client_anim_accum -= ANIM_FIXED_DT;

		wl_list_for_each(c, &clients, link) {
			if (!c->animating)
				continue;
			c->anim_x = spring_step(c->anim_x, (float)c->target_geom.x, &c->vx, ANIM_FIXED_DT);
			c->anim_y = spring_step(c->anim_y, (float)c->target_geom.y, &c->vy, ANIM_FIXED_DT);
			if (fabsf(c->anim_x - (float)c->target_geom.x) < 0.5f
					&& fabsf(c->anim_y - (float)c->target_geom.y) < 0.5f
					&& fabsf(c->vx) < 2.0f && fabsf(c->vy) < 2.0f) {
				/* Settle: one full resize to finalize size/clip/scale. */
				c->animating = 0;
				c->vx = c->vy = 0;
				c->anim_x = c->target_geom.x;
				c->anim_y = c->target_geom.y;
				resize(c, c->target_geom, 0);
				continue;
			}
			/* Cheap per-frame move: reposition the client's scene node (borders
			 * and focus ring are its children) — no full resize(). */
			c->geom.x = (int)lroundf(c->anim_x);
			c->geom.y = (int)lroundf(c->anim_y);
			if (c->scene) {
				/* Fullscreen/maximized/docked clients live in screen space
				 * already (see client_apply_zoom_frame()'s matching bypass)
				 * — applying the world->screen camera transform on top of
				 * that here would double-transform them. */
				if (c->isfullscreen || c->ismaximized || c->docked)
					wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
				else
					wlr_scene_node_set_position(&c->scene->node,
							WORLD_TO_SCREEN_X(c->mon, c->geom.x), WORLD_TO_SCREEN_Y(c->mon, c->geom.y));
			}
			still = 1;
		}
	}

	/* A call that only banked time (accumulator below ANIM_FIXED_DT) never
	 * entered the loop above, so `still` wouldn't reflect clients that are
	 * genuinely mid-animation but haven't had a full step yet this call —
	 * check directly, or rendermon() would stop asking for frames before
	 * enough real time has accumulated to actually finish the step. */
	if (!still) {
		wl_list_for_each(c, &clients, link) {
			if (c->animating) {
				still = 1;
				break;
			}
		}
	}

	if (!still)
		client_anim_have_last = 0;
	return still;
}

void
client_set_target_geom(Client *c, struct wlr_box geo)
{
	int moving;
	struct wlr_box now;

	if (!c)
		return;

	/* First placement (new window) or animations disabled: snap into place. */
	if (!c->anim_ready || anim_stiffness <= 0.0f) {
		c->target_geom = geo;
		c->anim_ready = 1;
		c->animating = 0;
		c->anim_x = geo.x;
		c->anim_y = geo.y;
		c->vx = c->vy = 0;
		resize(c, geo, 0);
		return;
	}

	/* Already gliding toward this exact target (e.g. re-arranged every camera
	 * frame during a pan): leave it to the frame stepper — no per-frame
	 * resize(). */
	if (c->animating && geo.x == c->target_geom.x && geo.y == c->target_geom.y
			&& geo.width == c->geom.width && geo.height == c->geom.height)
		return;

	moving = (geo.x != c->geom.x) || (geo.y != c->geom.y);
	c->target_geom = geo;

	if (moving || c->animating) {
		if (!c->animating) {
			c->anim_x = c->geom.x;
			c->anim_y = c->geom.y;
		}
		/* Apply the new size now at the current position; position springs. */
		now = (struct wlr_box){ (int)lroundf(c->anim_x), (int)lroundf(c->anim_y),
			geo.width, geo.height };
		c->animating = 1;
		resize(c, now, 0);
		if (c->mon && c->mon->wlr_output)
			wlr_output_schedule_frame(c->mon->wlr_output);
	} else if (geo.width != c->geom.width || geo.height != c->geom.height) {
		/* Not moving, but a genuine size change at a stable position: apply
		 * immediately, no spring needed. */
		resize(c, geo, 0);
	}
	/* else: position and size both already match — a true no-op. Without this
	 * check, any caller that resizes a batch of clients (e.g. group-drag)
	 * pays a full resize() — border/clip/buffer-scale recompute — even for
	 * members nothing actually changed about. */
}
