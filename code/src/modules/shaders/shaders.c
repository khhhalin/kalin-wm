/* shaders.c — GPU fragment-shader post-processing for kalin-wm.
 *
 * Design intent, phasing, and risks: obsidian/plan/shaders.md.
 *
 * The scene-graph (wlr_scene) exposes no per-node GLSL hook, so this keeps the
 * scene render intact and drops to raw GLES2 only at the compositing stage:
 *   1. wlr_scene renders the frame into a per-monitor offscreen swapchain
 *      (wlr_scene_output_build_state with a custom .swapchain);
 *   2. a fullscreen fragment-shader pass samples that offscreen texture and
 *      writes an output-swapchain buffer's FBO (raw GLES2, wlroots' EGL ctx);
 *   3. that buffer is committed to the real output.
 *
 * Phase 0 ships only a passthrough fragment shader — enabling the pass must be
 * visually identical to a plain wlr_scene_output_commit(). Custom GLSL needs
 * the GLES2 renderer; on Pixman/Vulkan the whole subsystem stays disabled and
 * shaders_render_output() returns false so rendermon() uses the normal path.
 *
 * Separately-compiled TU: links against dwl.c's externed globals (drw, alloc)
 * via kalin.h, like capture.c. */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drm_fourcc.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>

#include <wlr/render/drm_format_set.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "kalin.h"
#include "shaders.h"

/* Per-monitor offscreen render target. Stored opaquely in Monitor.shader_state
 * so the GL types don't leak into kalin.h. */
struct shader_mon {
	struct wlr_swapchain *offscreen;
	int width, height;
};

static bool available;       /* GLES2 present and program compiled */
static bool output_enabled;  /* config master switch for the output pass */
static struct wlr_egl *egl;
static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;
static GLuint program;
static GLint attr_pos, attr_uv, uni_tex;
static char shader_dir[512];

/* The vertex stage is fixed infrastructure (a fullscreen quad), not a
 * user-authored effect, so it lives here rather than in a .vert file. */
static const char *VERT_SRC =
	"attribute vec2 a_pos;\n"
	"attribute vec2 a_uv;\n"
	"varying vec2 v_uv;\n"
	"void main() {\n"
	"	v_uv = a_uv;\n"
	"	gl_Position = vec4(a_pos, 0.0, 1.0);\n"
	"}\n";

/* --- EGL context save/restore --------------------------------------------
 * Raw GL needs wlroots' renderer context current. It is global state shared
 * with the renderer, so save whatever was current and put it back after. The
 * gles2 renderer also save/restores around its own passes, so this is belt
 * and braces, but keeping it tidy avoids surprising the next renderer op. */
struct egl_saved {
	EGLDisplay dpy;
	EGLContext ctx;
	EGLSurface draw, read;
};

static void
egl_enter(struct egl_saved *s)
{
	s->dpy = eglGetCurrentDisplay();
	s->ctx = eglGetCurrentContext();
	s->draw = eglGetCurrentSurface(EGL_DRAW);
	s->read = eglGetCurrentSurface(EGL_READ);
	eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context);
}

static void
egl_leave(const struct egl_saved *s)
{
	eglMakeCurrent(s->dpy != EGL_NO_DISPLAY ? s->dpy : egl_display,
			s->draw, s->read, s->ctx);
}

static char *
read_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	long n;
	char *buf;
	size_t rd;

	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0) {
		fclose(f);
		return NULL;
	}
	rewind(f);
	if (!(buf = malloc((size_t)n + 1))) {
		fclose(f);
		return NULL;
	}
	rd = fread(buf, 1, (size_t)n, f);
	fclose(f);
	buf[rd] = '\0';
	return buf;
}

static GLuint
compile_shader(GLenum type, const char *src, const char *label)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;

	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char info[512] = {0};
		glGetShaderInfoLog(s, sizeof(info), NULL, info);
		wlr_log(WLR_ERROR, "shaders: %s compile failed: %s", label, info);
		glDeleteShader(s);
		return 0;
	}
	return s;
}

/* Build the GLSL program from the built-in vertex shader + the fragment file.
 * Assumes an EGL context is already current. Returns true on success. */
static bool
build_program(void)
{
	char path[600];
	char *frag;
	GLuint vs, fs;
	GLint linked = 0;

	snprintf(path, sizeof(path), "%s/passthrough.frag", shader_dir);
	if (!(frag = read_file(path))) {
		wlr_log(WLR_ERROR, "shaders: cannot read %s", path);
		return false;
	}
	vs = compile_shader(GL_VERTEX_SHADER, VERT_SRC, "vertex");
	fs = compile_shader(GL_FRAGMENT_SHADER, frag, "passthrough.frag");
	free(frag);
	if (!vs || !fs) {
		if (vs)
			glDeleteShader(vs);
		if (fs)
			glDeleteShader(fs);
		return false;
	}

	program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	glDeleteShader(vs);
	glDeleteShader(fs);

	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked) {
		char info[512] = {0};
		glGetProgramInfoLog(program, sizeof(info), NULL, info);
		wlr_log(WLR_ERROR, "shaders: program link failed: %s", info);
		glDeleteProgram(program);
		program = 0;
		return false;
	}
	attr_pos = glGetAttribLocation(program, "a_pos");
	attr_uv = glGetAttribLocation(program, "a_uv");
	uni_tex = glGetUniformLocation(program, "tex");
	return attr_pos >= 0 && attr_uv >= 0 && uni_tex >= 0;
}

void
shaders_init(struct wlr_renderer *renderer, int cfg_output_enabled,
		const char *cfg_dir)
{
	const char *env = getenv("KALIN_SHADER_DIR");
	const char *dir = env ? env : (cfg_dir ? cfg_dir : "shaders");
	struct egl_saved saved;

	output_enabled = cfg_output_enabled != 0;
	snprintf(shader_dir, sizeof(shader_dir), "%s", dir);

	if (!renderer || !wlr_renderer_is_gles2(renderer)) {
		wlr_log(WLR_INFO, "shaders: renderer is not GLES2 "
				"(set WLR_RENDERER=gles2 for shaders); disabled");
		return;
	}
	if (!(egl = wlr_gles2_renderer_get_egl(renderer)))
		return;
	egl_display = wlr_egl_get_display(egl);
	egl_context = wlr_egl_get_context(egl);
	if (egl_display == EGL_NO_DISPLAY || egl_context == EGL_NO_CONTEXT) {
		egl_display = EGL_NO_DISPLAY;
		return;
	}

	egl_enter(&saved);
	available = build_program();
	egl_leave(&saved);

	wlr_log(WLR_INFO, "shaders: GLES2 %s; output pass %s (dir=%s)",
			available ? "ready" : "init FAILED",
			output_enabled ? "ENABLED" : "available (disabled)",
			shader_dir);
}

static struct shader_mon *
mon_state(struct Monitor *m)
{
	if (!m->shader_state)
		m->shader_state = calloc(1, sizeof(struct shader_mon));
	return m->shader_state;
}

/* (Re)create the offscreen swapchain when missing or the output resized. */
static bool
ensure_offscreen(struct shader_mon *sm, struct wlr_output *out, int w, int h)
{
	const struct wlr_drm_format_set *fmts;
	const struct wlr_drm_format *fmt;

	if (sm->offscreen && sm->width == w && sm->height == h)
		return true;
	if (sm->offscreen) {
		wlr_swapchain_destroy(sm->offscreen);
		sm->offscreen = NULL;
	}
	if (!(fmts = wlr_output_get_primary_formats(out, WLR_BUFFER_CAP_DMABUF)))
		return false;
	if (!(fmt = wlr_drm_format_set_get(fmts, DRM_FORMAT_XRGB8888)))
		return false;
	if (!(sm->offscreen = wlr_swapchain_create(alloc, w, h, fmt)))
		return false;
	sm->width = w;
	sm->height = h;
	return true;
}

/* Raw GLES2 fullscreen pass: sample `src` (the offscreen scene), write `dst`'s
 * FBO. Returns true on success. */
static bool
gl_pass(struct wlr_buffer *src, struct wlr_buffer *dst, int w, int h)
{
	struct wlr_texture *tex = wlr_texture_from_buffer(drw, src);
	struct wlr_gles2_texture_attribs ta;
	struct egl_saved saved;
	GLuint fbo;
	/* Triangle strip covering clip space, with V flipped: wlroots textures
	 * have a top-left origin while the GL FBO is bottom-left, so map clip
	 * bottom (-1) to texture top (v=1). NOTE: the vertical orientation is the
	 * single most likely first-run bug — verify on real hardware (Phase 0). */
	static const GLfloat pos[] = {-1.f, -1.f,  1.f, -1.f, -1.f,  1.f,  1.f,  1.f};
	static const GLfloat uv[]  = { 0.f,  1.f,  1.f,  1.f,  0.f,  0.f,  1.f,  0.f};

	if (!tex)
		return false;
	wlr_gles2_texture_get_attribs(tex, &ta);
	if (ta.target != GL_TEXTURE_2D) {
		/* Passthrough.frag samples a sampler2D; an external-OES offscreen
		 * texture would need a different shader. The offscreen render target
		 * is a plain 2D texture in practice, but bail safely if not. */
		wlr_log(WLR_ERROR, "shaders: offscreen texture is not GL_TEXTURE_2D");
		wlr_texture_destroy(tex);
		return false;
	}
	if (!(fbo = wlr_gles2_renderer_get_buffer_fbo(drw, dst))) {
		wlr_texture_destroy(tex);
		return false;
	}

	egl_enter(&saved);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, w, h);
	glDisable(GL_BLEND);
	glUseProgram(program);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, ta.tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glUniform1i(uni_tex, 0);

	glVertexAttribPointer(attr_pos, 2, GL_FLOAT, GL_FALSE, 0, pos);
	glEnableVertexAttribArray(attr_pos);
	glVertexAttribPointer(attr_uv, 2, GL_FLOAT, GL_FALSE, 0, uv);
	glEnableVertexAttribArray(attr_uv);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableVertexAttribArray(attr_pos);
	glDisableVertexAttribArray(attr_uv);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glFlush();
	egl_leave(&saved);

	wlr_texture_destroy(tex);
	return true;
}

bool
shaders_render_output(struct Monitor *m)
{
	struct shader_mon *sm;
	struct wlr_output *out;
	struct wlr_output_state scene_state, commit_state;
	struct wlr_scene_output_state_options opts = {0};
	struct wlr_swapchain *out_sc = NULL;
	struct wlr_buffer *out_buf;
	int w, h;
	bool ok;

	if (!available || !output_enabled)
		return false;
	if (!m || !m->wlr_output || !m->scene_output || !alloc || !drw)
		return false;
	out = m->wlr_output;
	w = out->width;
	h = out->height;
	if (w <= 0 || h <= 0)
		return false;

	sm = mon_state(m);
	if (!sm || !ensure_offscreen(sm, out, w, h))
		return false;

	/* 1. Render the scene into our offscreen swapchain (does not commit). */
	wlr_output_state_init(&scene_state);
	opts.swapchain = sm->offscreen;
	if (!wlr_scene_output_build_state(m->scene_output, &scene_state, &opts)
			|| !scene_state.buffer) {
		wlr_output_state_finish(&scene_state);
		return false;
	}

	/* 2. Acquire an output-swapchain buffer to shade into. */
	if (!wlr_output_configure_primary_swapchain(out, NULL, &out_sc)
			|| !out_sc) {
		wlr_output_state_finish(&scene_state);
		return false;
	}
	if (!(out_buf = wlr_swapchain_acquire(out_sc))) {
		wlr_output_state_finish(&scene_state);
		return false;
	}

	/* 3. Fragment-shader pass: offscreen scene -> output buffer. */
	ok = gl_pass(scene_state.buffer, out_buf, w, h);

	/* 4. Commit the shaded buffer to the real output. */
	if (ok) {
		wlr_output_state_init(&commit_state);
		wlr_output_state_set_buffer(&commit_state, out_buf);
		ok = wlr_output_commit_state(out, &commit_state);
		wlr_output_state_finish(&commit_state);
	}

	wlr_buffer_unlock(out_buf);
	wlr_output_state_finish(&scene_state); /* unlocks the offscreen buffer */
	return ok;
}

void
shaders_output_destroy(struct Monitor *m)
{
	struct shader_mon *sm = m ? m->shader_state : NULL;

	if (!sm)
		return;
	if (sm->offscreen)
		wlr_swapchain_destroy(sm->offscreen);
	free(sm);
	m->shader_state = NULL;
}
