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
#include <wlr/render/pass.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "kalin.h"
#include "shaders.h"
#include "window_shader_math.h"

/* Per-monitor offscreen render target. Stored opaquely in Monitor.shader_state
 * so the GL types don't leak into kalin.h. */
struct shader_mon {
	struct wlr_swapchain *offscreen;
	int width, height;
};

static bool available;       /* GLES2 present and output program compiled */
static bool output_enabled;  /* config master switch for the output pass */
static struct wlr_egl *egl;
static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLContext egl_context = EGL_NO_CONTEXT;
static GLuint program;
static GLint attr_pos, attr_uv, uni_tex;
static char shader_dir[512];

/* Per-window paper-mode program (shaders/paper.frag). Independent of the
 * output pass: it can be ready even when the output pass isn't, and vice
 * versa. paper_available gates the whole per-window path. */
static bool paper_available;
static GLuint paper_program;
static GLint paper_attr_pos, paper_attr_uv, paper_uni_tex;
static GLint paper_uni_strength, paper_uni_paper, paper_uni_ink, paper_uni_preserve;

/* Per-client offscreen state for the window-shade path. There is no Client
 * field to hang this on (kalin.h is owned elsewhere), so the module keeps its
 * own small registry keyed by the Client pointer. Windows are few, so a plain
 * singly-linked list is enough. Two swapchains: `raw` receives the composited
 * subtree, `shaded` receives paper.frag's output (read-then-write can't share
 * one buffer). `held` is the shaded buffer currently lent to the caller. */
struct shader_win {
	struct Client *client;
	struct wlr_swapchain *raw;
	struct wlr_swapchain *shaded;
	int width, height;
	struct wlr_buffer *held;
	struct shader_win *next;
};
static struct shader_win *win_list;

/* Fullscreen quad. quad_uv (V flipped: clip bottom -> texture top) was the
 * original shared assumption for both passes. The live GPU gate (2026-07-15,
 * Intel GLES2) proved it WRONG for the window path — the shaded overlay came
 * out upside-down — so paper_pass uses quad_uv_win (identity V: clip bottom ->
 * v=0). Empirically: texture-from-buffer sampling and FBO row order cancel out
 * on this path, no flip needed. The output pass keeps quad_uv until its own
 * gate run — its sink is scanout, not a sampled scene buffer, so its
 * orientation must be verified independently, not assumed from this result. */
static const GLfloat quad_pos[]    = {-1.f, -1.f,  1.f, -1.f, -1.f,  1.f,  1.f,  1.f};
static const GLfloat quad_uv[]     = { 0.f,  1.f,  1.f,  1.f,  0.f,  0.f,  1.f,  0.f};
static const GLfloat quad_uv_win[] = { 0.f,  0.f,  1.f,  0.f,  0.f,  1.f,  1.f,  1.f};

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

/* Link the built-in vertex shader with the fragment file `<shader_dir>/name`
 * into a program. Assumes an EGL context is already current. Returns the
 * program id, or 0 on any read/compile/link failure. */
static GLuint
compile_program(const char *name)
{
	char path[600];
	char *frag;
	GLuint vs, fs, prog;
	GLint linked = 0;

	snprintf(path, sizeof(path), "%s/%s", shader_dir, name);
	if (!(frag = read_file(path))) {
		wlr_log(WLR_ERROR, "shaders: cannot read %s", path);
		return 0;
	}
	vs = compile_shader(GL_VERTEX_SHADER, VERT_SRC, "vertex");
	fs = compile_shader(GL_FRAGMENT_SHADER, frag, name);
	free(frag);
	if (!vs || !fs) {
		if (vs)
			glDeleteShader(vs);
		if (fs)
			glDeleteShader(fs);
		return 0;
	}

	prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	glDeleteShader(vs);
	glDeleteShader(fs);

	glGetProgramiv(prog, GL_LINK_STATUS, &linked);
	if (!linked) {
		char info[512] = {0};
		glGetProgramInfoLog(prog, sizeof(info), NULL, info);
		wlr_log(WLR_ERROR, "shaders: %s link failed: %s", name, info);
		glDeleteProgram(prog);
		return 0;
	}
	return prog;
}

/* Compile the passthrough output program and resolve its locations. Assumes an
 * EGL context is current. Returns true on success. */
static bool
build_program(void)
{
	if (!(program = compile_program("passthrough.frag")))
		return false;
	attr_pos = glGetAttribLocation(program, "a_pos");
	attr_uv = glGetAttribLocation(program, "a_uv");
	uni_tex = glGetUniformLocation(program, "tex");
	return attr_pos >= 0 && attr_uv >= 0 && uni_tex >= 0;
}

/* Compile the paper-mode window program and resolve its locations. A missing
 * uniform location (-1) just means the driver optimised that uniform away;
 * glUniform* on -1 is a documented no-op, so only the vertex attributes and
 * the sampler are required. Assumes an EGL context is current. */
static bool
build_paper_program(void)
{
	if (!(paper_program = compile_program("paper.frag")))
		return false;
	paper_attr_pos = glGetAttribLocation(paper_program, "a_pos");
	paper_attr_uv = glGetAttribLocation(paper_program, "a_uv");
	paper_uni_tex = glGetUniformLocation(paper_program, "tex");
	paper_uni_strength = glGetUniformLocation(paper_program, "u_strength");
	paper_uni_paper = glGetUniformLocation(paper_program, "u_paper");
	paper_uni_ink = glGetUniformLocation(paper_program, "u_ink");
	paper_uni_preserve = glGetUniformLocation(paper_program, "u_preserve");
	return paper_attr_pos >= 0 && paper_attr_uv >= 0 && paper_uni_tex >= 0;
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
	paper_available = build_paper_program();
	egl_leave(&saved);

	wlr_log(WLR_INFO, "shaders: GLES2 output %s, paper %s; output pass %s (dir=%s)",
			available ? "ready" : "FAILED",
			paper_available ? "ready" : "FAILED",
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

	glVertexAttribPointer(attr_pos, 2, GL_FLOAT, GL_FALSE, 0, quad_pos);
	glEnableVertexAttribArray(attr_pos);
	glVertexAttribPointer(attr_uv, 2, GL_FLOAT, GL_FALSE, 0, quad_uv);
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

/* ==========================================================================
 * Per-window composite shader (paper mode).
 *
 * The window's scene subtree is composited into an offscreen `raw` buffer with
 * a normal wlroots render pass, paper.frag is run over it into a `shaded`
 * buffer with a raw-GLES2 pass, and `shaded` is lent back to the caller to
 * reinject as a wlr_scene_buffer. Two buffers because a fragment pass can't
 * read and write the same texture. Orientation: quad_uv_win (identity V),
 * GPU-verified upright at the 2026-07-15 live gate — see the quad note above.
 * ========================================================================== */

struct shader_paper_params
shaders_paper_defaults(void)
{
	return ws_paper_defaults();
}

static struct shader_win *
win_find(struct Client *c)
{
	struct shader_win *sw;

	for (sw = win_list; sw; sw = sw->next)
		if (sw->client == c)
			return sw;
	return NULL;
}

static struct shader_win *
win_get(struct Client *c)
{
	struct shader_win *sw = win_find(c);

	if (sw)
		return sw;
	if (!(sw = calloc(1, sizeof(*sw))))
		return NULL;
	sw->client = c;
	sw->next = win_list;
	win_list = sw;
	return sw;
}

/* Allocate a swapchain of `w`x`h` from the output's allocator, preferring an
 * alpha format (windows can be translucent) and falling back to opaque. */
static struct wlr_swapchain *
make_swapchain(struct wlr_output *out, int w, int h)
{
	const struct wlr_drm_format_set *fmts;
	const struct wlr_drm_format *fmt;

	if (!(fmts = wlr_output_get_primary_formats(out, WLR_BUFFER_CAP_DMABUF)))
		return NULL;
	if (!(fmt = wlr_drm_format_set_get(fmts, DRM_FORMAT_ARGB8888)))
		fmt = wlr_drm_format_set_get(fmts, DRM_FORMAT_XRGB8888);
	if (!fmt)
		return NULL;
	return wlr_swapchain_create(alloc, w, h, fmt);
}

/* (Re)create the raw + shaded swapchains when missing or the window resized. */
static bool
win_ensure(struct shader_win *sw, struct wlr_output *out, int w, int h)
{
	if (sw->raw && sw->shaded && sw->width == w && sw->height == h)
		return true;
	if (sw->raw) {
		wlr_swapchain_destroy(sw->raw);
		sw->raw = NULL;
	}
	if (sw->shaded) {
		wlr_swapchain_destroy(sw->shaded);
		sw->shaded = NULL;
	}
	if (!(sw->raw = make_swapchain(out, w, h)))
		return false;
	if (!(sw->shaded = make_swapchain(out, w, h))) {
		wlr_swapchain_destroy(sw->raw);
		sw->raw = NULL;
		return false;
	}
	sw->width = w;
	sw->height = h;
	return true;
}

/* A scene-buffer node's on-screen size: its scaled destination if one is set,
 * else the source buffer's own pixel size. */
static void
buffer_render_size(struct wlr_scene_buffer *sb, int *w, int *h)
{
	if (sb->dst_width > 0 && sb->dst_height > 0) {
		*w = sb->dst_width;
		*h = sb->dst_height;
	} else if (sb->buffer) {
		*w = sb->buffer->width;
		*h = sb->buffer->height;
	} else {
		*w = 0;
		*h = 0;
	}
}

/* Pass 1: bounding box of the subtree's buffer nodes, in layout coords. */
struct bounds_ctx {
	int x0, y0, x1, y1;
	bool any;
};

static void
bounds_iter(struct wlr_scene_buffer *sb, int sx, int sy, void *data)
{
	struct bounds_ctx *b = data;
	int w, h;

	if (!sb || !sb->buffer)
		return;
	buffer_render_size(sb, &w, &h);
	if (w <= 0 || h <= 0)
		return;
	if (!b->any) {
		b->x0 = sx;
		b->y0 = sy;
		b->x1 = sx + w;
		b->y1 = sy + h;
		b->any = true;
		return;
	}
	if (sx < b->x0)
		b->x0 = sx;
	if (sy < b->y0)
		b->y0 = sy;
	if (sx + w > b->x1)
		b->x1 = sx + w;
	if (sy + h > b->y1)
		b->y1 = sy + h;
}

/* Pass 2: draw each buffer node into the offscreen render pass, positioned
 * relative to the window origin. */
#define COMPOSITE_MAX_TEX 64
struct composite_ctx {
	struct wlr_render_pass *pass;
	int ox, oy;
	struct wlr_texture *tex[COMPOSITE_MAX_TEX]; /* kept alive until submit */
	int n;
};

static void
composite_iter(struct wlr_scene_buffer *sb, int sx, int sy, void *data)
{
	struct composite_ctx *cc = data;
	struct wlr_texture *tex;
	struct wlr_render_texture_options opt = {0};
	int w, h;

	if (!sb || !sb->buffer)
		return;
	buffer_render_size(sb, &w, &h);
	if (w <= 0 || h <= 0)
		return;
	if (cc->n >= COMPOSITE_MAX_TEX)
		return;
	if (!(tex = wlr_texture_from_buffer(drw, sb->buffer)))
		return;

	opt.texture = tex;
	opt.dst_box.x = sx - cc->ox;
	opt.dst_box.y = sy - cc->oy;
	opt.dst_box.width = w;
	opt.dst_box.height = h;
	/* Honour a node's crop (source box) if it set one; a zero box means the
	 * whole texture. */
	if (sb->src_box.width > 0 && sb->src_box.height > 0)
		opt.src_box = sb->src_box;
	opt.transform = sb->transform;
	opt.filter_mode = sb->filter_mode;
	opt.alpha = &sb->opacity;
	wlr_render_pass_add_texture(cc->pass, &opt);

	/* Keep the texture alive until after submit: the render pass records the
	 * op and executes it at submit, so destroying it here rendered nothing. */
	cc->tex[cc->n++] = tex;
}

/* Composite client c's subtree (origin ox,oy) into dst via a wlroots pass. */
static bool
composite_subtree(struct Client *c, struct wlr_buffer *dst, int ox, int oy,
		int w, int h)
{
	struct wlr_render_pass *pass;
	struct composite_ctx cc = { .ox = ox, .oy = oy };
	struct wlr_render_rect_options clear = {0};
	bool ok;
	int i;

	if (!(pass = wlr_renderer_begin_buffer_pass(drw, dst, NULL)))
		return false;
	/* Clear the (uninitialised) offscreen to transparent before compositing;
	 * blend NONE makes it a plain overwrite. */
	clear.box.width = w;
	clear.box.height = h;
	clear.blend_mode = WLR_RENDER_BLEND_MODE_NONE;
	wlr_render_pass_add_rect(pass, &clear);

	cc.pass = pass;
	wlr_scene_node_for_each_buffer(&c->scene->node, composite_iter, &cc);
	ok = wlr_render_pass_submit(pass);
	for (i = 0; i < cc.n; i++)
		wlr_texture_destroy(cc.tex[i]);
	return ok;
}

/* Raw-GLES2 paper.frag pass: sample `src` (composited window), write `dst`'s
 * FBO, wiring the paper uniforms. Mirrors gl_pass but with the paper program.
 * Returns true on success. */
static bool
paper_pass(struct wlr_buffer *src, struct wlr_buffer *dst, int w, int h,
		const struct shader_paper_params *p)
{
	struct wlr_texture *tex = wlr_texture_from_buffer(drw, src);
	struct wlr_gles2_texture_attribs ta;
	struct egl_saved saved;
	GLuint fbo;

	if (!tex)
		return false;
	wlr_gles2_texture_get_attribs(tex, &ta);
	if (ta.target != GL_TEXTURE_2D) {
		wlr_log(WLR_ERROR, "shaders: window texture is not GL_TEXTURE_2D");
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
	glUseProgram(paper_program);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, ta.tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glUniform1i(paper_uni_tex, 0);
	glUniform1f(paper_uni_strength, p->strength);
	glUniform3f(paper_uni_paper, p->paper[0], p->paper[1], p->paper[2]);
	glUniform3f(paper_uni_ink, p->ink[0], p->ink[1], p->ink[2]);
	glUniform1f(paper_uni_preserve, p->preserve);

	glVertexAttribPointer(paper_attr_pos, 2, GL_FLOAT, GL_FALSE, 0, quad_pos);
	glEnableVertexAttribArray(paper_attr_pos);
	glVertexAttribPointer(paper_attr_uv, 2, GL_FLOAT, GL_FALSE, 0, quad_uv_win);
	glEnableVertexAttribArray(paper_attr_uv);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableVertexAttribArray(paper_attr_pos);
	glDisableVertexAttribArray(paper_attr_uv);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glFlush();
	egl_leave(&saved);

	wlr_texture_destroy(tex);
	return true;
}

struct wlr_buffer *
shaders_window_shade(struct Client *c, const struct shader_paper_params *params)
{
	struct shader_paper_params p = ws_paper_normalize(params);
	struct bounds_ctx b = {0};
	struct shader_win *sw;
	struct wlr_output *out;
	struct wlr_buffer *raw_buf, *shaded_buf;
	int w, h;

	if (!paper_available || !drw || !alloc)
		return NULL;
	if (!c || !c->scene || !c->mon || !c->mon->wlr_output)
		return NULL;
	out = c->mon->wlr_output;

	wlr_scene_node_for_each_buffer(&c->scene->node, bounds_iter, &b);
	if (!b.any || !ws_dim_valid(b.x1 - b.x0, b.y1 - b.y0))
		return NULL;
	w = ws_clamp_dim(b.x1 - b.x0);
	h = ws_clamp_dim(b.y1 - b.y0);

	if (!(sw = win_get(c)) || !win_ensure(sw, out, w, h))
		return NULL;

	if (!(raw_buf = wlr_swapchain_acquire(sw->raw)))
		return NULL;
	if (!composite_subtree(c, raw_buf, b.x0, b.y0, w, h)) {
		wlr_buffer_unlock(raw_buf);
		return NULL;
	}

	if (!(shaded_buf = wlr_swapchain_acquire(sw->shaded))) {
		wlr_buffer_unlock(raw_buf);
		return NULL;
	}
	if (!paper_pass(raw_buf, shaded_buf, w, h, &p)) {
		wlr_buffer_unlock(shaded_buf);
		wlr_buffer_unlock(raw_buf);
		return NULL;
	}
	wlr_buffer_unlock(raw_buf); /* raw is only an intermediate */

	/* Lend `shaded_buf` to the caller: drop the previous frame's lease and
	 * keep this one locked until the next shade/release for this client. */
	if (sw->held)
		wlr_buffer_unlock(sw->held);
	sw->held = shaded_buf;
	return shaded_buf;
}

int
shaders_capture_window(struct Client *c, unsigned char **out_data,
		int *out_w, int *out_h, size_t *out_stride)
{
	struct bounds_ctx b = {0};
	struct wlr_output *out;
	struct wlr_swapchain *sc = NULL;
	struct wlr_buffer *buf = NULL;
	struct wlr_texture *tex = NULL;
	unsigned char *data = NULL;
	size_t stride;
	int w, h, ok = 0;

	/* Only needs the renderer + allocator, NOT paper_available: composite_subtree()
	 * uses a plain wlr_renderer pass (renderer-agnostic), so a single-window shot
	 * works even where the paper.frag GLES program didn't compile (e.g. Pixman). */
	if (!drw || !alloc)
		return 0;
	if (!c || !c->scene || !c->mon || !c->mon->wlr_output)
		return 0;
	out = c->mon->wlr_output;

	/* Native subtree bounds — local, camera-independent (see bounds_iter). */
	wlr_scene_node_for_each_buffer(&c->scene->node, bounds_iter, &b);
	if (!b.any || !ws_dim_valid(b.x1 - b.x0, b.y1 - b.y0))
		return 0;
	w = ws_clamp_dim(b.x1 - b.x0);
	h = ws_clamp_dim(b.y1 - b.y0);

	/* Throwaway swapchain so a capture never disturbs paper-mode's cached
	 * per-client swapchains (win_ensure/win_get). */
	if (!(sc = make_swapchain(out, w, h)))
		return 0;
	if (!(buf = wlr_swapchain_acquire(sc)))
		goto out;
	if (!composite_subtree(c, buf, b.x0, b.y0, w, h))
		goto out;

	stride = (size_t)w * 4;
	data = malloc(stride * (size_t)h);
	tex = wlr_texture_from_buffer(drw, buf);
	if (tex && data) {
		struct wlr_texture_read_pixels_options opts = {
			.data = data,
			.format = DRM_FORMAT_XRGB8888,
			.stride = (uint32_t)stride,
		};
		ok = wlr_texture_read_pixels(tex, &opts);
	}
	if (ok) {
		*out_data = data;
		*out_w = w;
		*out_h = h;
		*out_stride = stride;
		data = NULL; /* handed to caller */
	}
out:
	if (tex)
		wlr_texture_destroy(tex);
	free(data);
	if (buf)
		wlr_buffer_unlock(buf);
	if (sc)
		wlr_swapchain_destroy(sc);
	return ok;
}

void
shaders_window_release(struct Client *c)
{
	struct shader_win *sw = win_find(c), **pp;

	if (!sw)
		return;
	for (pp = &win_list; *pp; pp = &(*pp)->next) {
		if (*pp == sw) {
			*pp = sw->next;
			break;
		}
	}
	if (sw->held)
		wlr_buffer_unlock(sw->held);
	if (sw->raw)
		wlr_swapchain_destroy(sw->raw);
	if (sw->shaded)
		wlr_swapchain_destroy(sw->shaded);
	free(sw);
}
