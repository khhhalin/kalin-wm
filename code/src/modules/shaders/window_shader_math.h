#ifndef KALIN_WINDOW_SHADER_MATH_H
#define KALIN_WINDOW_SHADER_MATH_H

/* Pure, GPU-free helpers for the per-window paper-mode shader path in
 * shaders.c: offscreen-size clamping and paper-uniform normalisation. Kept in
 * a header of static-inline functions with no wlroots/GL dependency so a
 * standalone unit test can exercise them without a renderer (see
 * window_shader_math_test.c). Design intent: obsidian/plan/shaders.md. */

#include <stdbool.h>

#include "shaders.h"   /* struct shader_paper_params (public API type) */

/* Hard cap on an offscreen dimension. A window subtree's bounding box drives
 * the offscreen buffer size; clamp so a bogus/huge extent can never ask the
 * allocator for a pathological buffer. 8192 comfortably covers real displays
 * (GL_MAX_TEXTURE_SIZE is >= 8192 on the GLES2 hardware this targets). */
#define WS_MAX_DIM 8192

/* Clamp a pixel dimension into [1, WS_MAX_DIM]. A zero/negative extent means
 * "nothing to shade" — the caller checks ws_dim_valid() first and skips; this
 * only guards the value handed to the allocator. */
static inline int
ws_clamp_dim(int v)
{
	if (v < 1)
		return 1;
	if (v > WS_MAX_DIM)
		return WS_MAX_DIM;
	return v;
}

/* A window bounding box is renderable only if both extents are positive. */
static inline bool
ws_dim_valid(int w, int h)
{
	return w > 0 && h > 0;
}

static inline float
ws_clampf(float v, float lo, float hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

/* Sensible baked-in defaults (config wiring is a later task): a warm off-white
 * page, warm near-black ink, most of the effect on, hues mostly preserved. */
static inline struct shader_paper_params
ws_paper_defaults(void)
{
	struct shader_paper_params p = {
		.strength = 0.85f,
		.paper = { 0.96f, 0.93f, 0.84f },
		.ink = { 0.14f, 0.12f, 0.09f },
		.preserve = 0.60f,
	};
	return p;
}

/* Clamp every field into the range the fragment shader assumes ([0,1]); a
 * NULL params pointer yields the defaults. Idempotent. */
static inline struct shader_paper_params
ws_paper_normalize(const struct shader_paper_params *in)
{
	struct shader_paper_params p = in ? *in : ws_paper_defaults();
	int i;

	p.strength = ws_clampf(p.strength, 0.0f, 1.0f);
	p.preserve = ws_clampf(p.preserve, 0.0f, 1.0f);
	for (i = 0; i < 3; i++) {
		p.paper[i] = ws_clampf(p.paper[i], 0.0f, 1.0f);
		p.ink[i] = ws_clampf(p.ink[i], 0.0f, 1.0f);
	}
	return p;
}

/* Map the papyrus knob (yellow, 0..1) to shader params, blending between a
 * crisp warm page and an aged tan. `base` is the yellow=1 endpoint (its .paper
 * is the crisp page colour, .strength the full-effect strength, .ink/.preserve
 * pass through); `aged` is the page colour yellow=1 warms toward. Strength is
 * eased with smoothstep so low yellow stays subtle; the page colour lerps
 * crisp->aged on the same curve. yellow<=0 yields strength 0 (passthrough).
 * Pure and clamp-safe so window_shader_math_test can exercise it. */
static inline struct shader_paper_params
ws_paper_from_yellow(float yellow, const struct shader_paper_params *base,
		const float aged[3])
{
	struct shader_paper_params b = ws_paper_normalize(base);
	struct shader_paper_params p = b;
	float y = ws_clampf(yellow, 0.0f, 1.0f);
	float e = y * y * (3.0f - 2.0f * y);   /* smoothstep(0,1,y) */
	int i;

	p.strength = b.strength * e;
	for (i = 0; i < 3; i++)
		p.paper[i] = b.paper[i] + (aged[i] - b.paper[i]) * e;
	return ws_paper_normalize(&p);
}

#endif /* KALIN_WINDOW_SHADER_MATH_H */
