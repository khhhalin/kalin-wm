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
struct Monitor;

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

#endif /* KALIN_SHADERS_H */
