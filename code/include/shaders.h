#ifndef KALIN_SHADERS_H
#define KALIN_SHADERS_H

#include <stdbool.h>

/* GPU fragment-shader post-processing. Design intent + phasing live in
 * obsidian/plan/shaders.md. This is the Phase 0 infrastructure: an offscreen
 * render + a fullscreen fragment-shader pass that composites the scene to the
 * output. Custom GLSL requires the GLES2 renderer (WLR_RENDERER=gles2); on
 * Pixman/Vulkan the subsystem disables itself and callers fall back to the
 * plain wlr_scene_output_commit() path. Opaque here so the GL/EGL headers stay
 * confined to shaders.c and out of the rest of the tree. */

struct wlr_renderer;
struct wlr_buffer;
struct Monitor;
struct Client;

/* Detect GLES2, capture the renderer's EGL handles, and compile the shader
 * program from `dir` (env KALIN_SHADER_DIR overrides). `output_enabled` is the
 * config master switch for the camera/output pass. On any unsupported renderer
 * or compile failure this logs once and leaves the subsystem disabled — it
 * never aborts the compositor. Call once from setup(), after `drw` exists. */
void shaders_init(struct wlr_renderer *renderer, int output_enabled,
		const char *dir);

/* Render monitor m through the shader pipeline: scene -> offscreen buffer ->
 * fragment-shader pass -> output buffer -> commit. Returns true if it handled
 * the output commit; false if shaders are disabled/unavailable or any step
 * failed, in which case the caller MUST fall back to wlr_scene_output_commit().
 * Returning false is cheap when disabled (the common, default case). */
bool shaders_render_output(struct Monitor *m);

/* Release monitor m's offscreen resources. Call from the monitor teardown. */
void shaders_output_destroy(struct Monitor *m);

/* --- Per-window composite shader (paper mode) ----------------------------
 * Composite-time window effect: the window's whole scene subtree is rendered
 * to an offscreen buffer, a fragment shader (shaders/paper.frag) is run over
 * it, and the shaded result is handed back for the caller to reinject as a
 * wlr_scene_buffer. Design intent + phasing: obsidian/plan/shaders.md. POD (no
 * GL types) so it stays in this opaque header. */
struct shader_paper_params {
	float strength;   /* 0 = passthrough, 1 = full paper */
	float paper[3];   /* colour a pure-white page maps to */
	float ink[3];     /* colour pure black maps to */
	float preserve;   /* how strongly saturated pixels keep their hue */
};

/* Baked-in paper-mode defaults (warm off-white page, warm ink). The config
 * wiring that overrides these is a later task; callers may start from here. */
struct shader_paper_params shaders_paper_defaults(void);

/* Render client c's scene subtree (root surface + subsurfaces + popups) to an
 * offscreen buffer, run paper.frag over it with `params` (NULL = defaults,
 * fields are clamped to [0,1]), and return the shaded buffer for the caller to
 * set on a wlr_scene_buffer node. Returns NULL if shaders are unavailable
 * (Pixman/Vulkan, program not compiled) or any step fails — the caller then
 * leaves the window unshaded; this never aborts the compositor.
 *
 * The returned buffer is borrowed: it stays valid until the next
 * shaders_window_shade() or shaders_window_release() for the SAME client. The
 * caller must reinject it (wlr_scene_buffer_set_buffer, which takes its own
 * lock) within the same frame and must NOT unlock or destroy it. */
struct wlr_buffer *shaders_window_shade(struct Client *c,
		const struct shader_paper_params *params);

/* Release client c's per-window offscreen resources. Call on unmap/destroy,
 * AFTER detaching the shaded buffer from its scene node (set NULL or destroy
 * the node) so nothing references a buffer this frees. Safe to call for a
 * client that was never shaded. */
void shaders_window_release(struct Client *c);

#endif /* KALIN_SHADERS_H */
