/* Standalone unit tests for the pure, GPU-free helpers in
 * window_shader_math.h (offscreen-size clamping + paper-uniform
 * normalisation). No wlroots/GL dependency, so this links on its own.
 *
 * NOT yet wired into `make test-unit` (that target lives in the Makefile,
 * which is out of scope for the paper-shader-core task). Run it directly:
 *
 *   gcc -std=c99 -Wall -Wextra -Wshadow -O1 -g -Icode/include \
 *       -o /tmp/ws_math_test \
 *       code/src/modules/shaders/window_shader_math_test.c && /tmp/ws_math_test
 *
 * Keeper: add an equivalent line to the test-unit target when wiring the
 * per-window shader path into dwl.c. */

#include <stdio.h>
#include <math.h>
#include <assert.h>

#include "../../../src/modules/shaders/window_shader_math.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
		failures++; \
	} \
} while (0)

static int
feq(float a, float b)
{
	return fabsf(a - b) < 1e-6f;
}

static void
test_clamp_dim(void)
{
	printf("clamp_dim...\n");
	CHECK(ws_clamp_dim(-5) == 1);
	CHECK(ws_clamp_dim(0) == 1);
	CHECK(ws_clamp_dim(1) == 1);
	CHECK(ws_clamp_dim(1920) == 1920);
	CHECK(ws_clamp_dim(WS_MAX_DIM) == WS_MAX_DIM);
	CHECK(ws_clamp_dim(WS_MAX_DIM + 1) == WS_MAX_DIM);
	CHECK(ws_clamp_dim(1 << 20) == WS_MAX_DIM);
}

static void
test_dim_valid(void)
{
	printf("dim_valid...\n");
	CHECK(ws_dim_valid(1, 1));
	CHECK(ws_dim_valid(1920, 1080));
	CHECK(!ws_dim_valid(0, 100));
	CHECK(!ws_dim_valid(100, 0));
	CHECK(!ws_dim_valid(-1, 100));
	CHECK(!ws_dim_valid(0, 0));
}

static void
test_clampf(void)
{
	printf("clampf...\n");
	CHECK(feq(ws_clampf(-0.5f, 0.0f, 1.0f), 0.0f));
	CHECK(feq(ws_clampf(0.5f, 0.0f, 1.0f), 0.5f));
	CHECK(feq(ws_clampf(1.5f, 0.0f, 1.0f), 1.0f));
	CHECK(feq(ws_clampf(2.0f, -1.0f, 3.0f), 2.0f));
}

static void
test_paper_defaults(void)
{
	struct shader_paper_params d = ws_paper_defaults();

	printf("paper_defaults...\n");
	/* Defaults must already be in-range (normalize is a no-op on them). */
	CHECK(d.strength >= 0.0f && d.strength <= 1.0f);
	CHECK(d.preserve >= 0.0f && d.preserve <= 1.0f);
	for (int i = 0; i < 3; i++) {
		CHECK(d.paper[i] >= 0.0f && d.paper[i] <= 1.0f);
		CHECK(d.ink[i] >= 0.0f && d.ink[i] <= 1.0f);
		/* Paper is a light page, ink is dark: sanity on the intent. */
		CHECK(d.paper[i] > d.ink[i]);
	}
}

static void
test_paper_normalize(void)
{
	struct shader_paper_params out_of_range = {
		.strength = 2.5f,
		.paper = { 1.7f, -0.2f, 0.5f },
		.ink = { -1.0f, 2.0f, 0.3f },
		.preserve = -0.4f,
	};
	struct shader_paper_params n = ws_paper_normalize(&out_of_range);
	struct shader_paper_params from_null = ws_paper_normalize(NULL);
	struct shader_paper_params d = ws_paper_defaults();
	struct shader_paper_params again;

	printf("paper_normalize...\n");
	CHECK(feq(n.strength, 1.0f));
	CHECK(feq(n.preserve, 0.0f));
	CHECK(feq(n.paper[0], 1.0f));
	CHECK(feq(n.paper[1], 0.0f));
	CHECK(feq(n.paper[2], 0.5f));
	CHECK(feq(n.ink[0], 0.0f));
	CHECK(feq(n.ink[1], 1.0f));
	CHECK(feq(n.ink[2], 0.3f));

	/* NULL params yields the defaults. */
	CHECK(feq(from_null.strength, d.strength));
	CHECK(feq(from_null.preserve, d.preserve));

	/* Idempotent: normalizing a normalized set changes nothing. */
	again = ws_paper_normalize(&n);
	CHECK(feq(again.strength, n.strength));
	CHECK(feq(again.paper[0], n.paper[0]));
	CHECK(feq(again.ink[1], n.ink[1]));
}

int
main(void)
{
	printf("=== window_shader_math tests ===\n");
	test_clamp_dim();
	test_dim_valid();
	test_clampf();
	test_paper_defaults();
	test_paper_normalize();
	if (failures) {
		printf("FAILED: %d check(s)\n", failures);
		return 1;
	}
	printf("OK: all checks passed\n");
	return 0;
}
