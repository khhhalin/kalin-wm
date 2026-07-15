# shaders

- **Status: Phase 0 output pass + per-window paper mode fully wired in tree,
  NOT yet GPU-verified (2026-07-15).** GPU fragment-shader post-processing.
  Design intent + full phasing: [[shaders]] in `plan/`. This note is the
  as-built record.
- As-built: (a) Phase 0 output pass — offscreen-render + fragment-pass plumbing
  and a passthrough shader; (b) the per-window composite-shader machinery
  (subtree → offscreen → paper.frag → shaded buffer) with paper.frag's uniforms
  wired; (c) the dwl.c driver (`paper-window-bind`): `Client.paper_mode` +
  `Super+i` `toggle-paper` bind + `Rule.paper` appid column, driven per-frame
  from `rendermon()` via `client_apply_paper()` — shade, reinject as an
  input-transparent overlay above the still-enabled surface
  (`point_accepts_input` returns false, so `xytonode()` falls through to the
  real surface and input keeps working), tear down on toggle-off/unmap.
  Uniform defaults live in `config.def.h` (`paper_strength/color/ink/preserve`).
- **Caveat found at the gate:** the live gitignored `code/config/config.h` does
  not regenerate on merge — new config symbols (the paper block) must be ported
  into it by hand or the build breaks. Also `shaders_dir` is CWD-relative and
  the `kalinwm` launcher doesn't run from the repo root, so the live compositor
  logs "cannot read shaders/passthrough.frag" and self-disables — set
  `KALIN_SHADER_DIR` (or install the .frag files) before the live GPU gate.

## Where it lives

- `code/src/modules/shaders/shaders.c` — the module (separate TU, links dwl.c
  globals `drw`/`alloc` via [[compile-time-config|kalin.h]], like `capture.c`).
  Holds both the output pass and the per-window paper path — the window path was
  kept in this TU rather than split into `window_shader.c` because the `Makefile`
  is out of the `paper-shader-core` task's scope, so a new TU would never be
  added to `SRCS` (and thus never compiled/verified). Splitting it out is a
  keeper follow-up.
- `code/src/modules/shaders/window_shader_math.h` — pure, GL-free helpers
  (offscreen-size clamp `ws_clamp_dim`/`ws_dim_valid`, paper-uniform
  `ws_paper_defaults`/`ws_paper_normalize`). Header of `static inline`s so a
  standalone test can exercise them without a renderer.
- `code/src/modules/shaders/window_shader_math_test.c` — unit test for the
  above. **Not yet in `make test-unit`** (Makefile out of scope); run manually,
  see the header comment. Keeper: wire it into the `test-unit` target.
- `code/include/shaders.h` — opaque API. Output pass: `shaders_init`,
  `shaders_render_output`, `shaders_output_destroy`. Per-window paper:
  `shaders_paper_defaults`, `shaders_window_shade`, `shaders_window_release`,
  plus the POD `struct shader_paper_params`. Keeps the GL/EGL headers out of the
  rest of the tree (forward-declares `struct Client`, `struct wlr_buffer`).
- `shaders/passthrough.frag` — the Phase 0 output shader (GLSL ES 1.00). The
  vertex stage is a built-in string in `shaders.c` (fixed fullscreen quad,
  shared by every pass, not a user-authored effect).
- `shaders/paper.frag` — the per-window paper-mode effect (GLSL ES 1.00): white
  → warm page, black → warm ink, saturated pixels keep their hue. Uniforms
  `u_strength`/`u_paper`/`u_ink`/`u_preserve` are now wired from C (were
  seed-only/unwired). The `.frag` itself was not modified.
- `config.h`/`config.def.h` — `shaders_output_enabled` (default **0**) +
  `shaders_dir` ("shaders"; env `KALIN_SHADER_DIR` overrides).
- `flake.nix` — added **`libGL`** to `deps` + devShell: nixpkgs `mesa` no longer
  ships `egl.pc`/`glesv2.pc`; `libGL` (libglvnd) provides them and links
  libEGL/libGLESv2. Without it the module doesn't compile.
- `Makefile` — `shaders.c` in SRCS; `pkg-config --cflags libdrm egl glesv2` +
  `--libs egl glesv2`.

## How it hooks in (the [[scene-graph]] bypass)

- `wlr_scene` has no per-node GLSL hook, so the scene render stays; raw GLES2 is
  used only at the compositing stage. Per-frame, when enabled + available:
  1. `rendermon()` calls `shaders_render_output(m)` instead of
     `wlr_scene_output_commit()`. On disabled/unavailable/any failure it returns
     false and the caller falls back to the plain commit — so **default off is
     byte-identical to before, by construction**.
  2. `wlr_scene_output_build_state(scene_output, &state, {.swapchain = per-mon
     offscreen})` renders the scene into a per-[[multi-camera|Monitor]] offscreen
     swapchain (XRGB8888, from `wlr_output_get_primary_formats`). Stored opaquely
     in `Monitor.shader_state`, freed in `cleanupmon()` via `shaders_output_destroy`.
  3. `gl_pass()`: `wlr_texture_from_buffer` the offscreen buffer → sample it with
     the program into an output-swapchain buffer's FBO
     (`wlr_gles2_renderer_get_buffer_fbo`), under wlroots' EGL context made
     current via `wlr_egl_get_display/get_context` + `eglMakeCurrent` (prior
     context saved/restored).
  4. `wlr_output_state_set_buffer` + `wlr_output_commit_state` present the shaded
     buffer.
- `shaders_init()` (in `setup()`, after `drw`): checks `wlr_renderer_is_gles2`,
  captures the EGL handles, compiles **both** programs (passthrough + paper)
  under a current context. Each has its own compiled-ok flag (`available` for
  the output pass, `paper_available` for the window path); one can be ready
  while the other failed. Any failure → logs once, that path stays disabled,
  never aborts.

## Per-window paper mode (composite-time window shader)

Follows the [[shaders|plan]] "composite-time window shader" path: the window's
own scene subtree is rendered offscreen, shaded, and handed back to reinject as
a `wlr_scene_buffer`. **Driven from `dwl.c`** (merged `paper-window-bind`):
`client_apply_paper()` runs per-frame from `rendermon()` for clients with
`paper_mode` (or a leftover overlay), disables the previous overlay before
capture (feedback exclusion), shades, and positions the overlay 1:1 over the
subtree bbox as a `c->scene` child above `scene_surface`; `unmapnotify()`
detaches the buffer and calls `shaders_window_release()` before the scene tree
is destroyed.

API (`shaders.h`, opaque):
- `struct shader_paper_params { float strength; float paper[3]; float ink[3];
  float preserve; }` — POD, matches `paper.frag`'s uniforms.
- `struct shader_paper_params shaders_paper_defaults(void)` — baked-in warm
  page (`strength 0.85`, `paper {0.96,0.93,0.84}`, `ink {0.14,0.12,0.09}`,
  `preserve 0.60`). Config override is a later task.
- `struct wlr_buffer *shaders_window_shade(struct Client *c,
  const struct shader_paper_params *params)` — the shade call. `params` NULL =
  defaults; fields clamped to `[0,1]`. Returns a **borrowed** shaded buffer
  (valid until the next shade/release for the same client) or NULL (unavailable
  / any failure → caller leaves the window unshaded). Caller reinjects it with
  `wlr_scene_buffer_set_buffer` (which takes its own lock) in the same frame and
  must not unlock/destroy it.
- `void shaders_window_release(struct Client *c)` — frees the client's offscreen
  resources; call on unmap/destroy **after** detaching the buffer from its scene
  node.

Per shade:
1. `wlr_scene_node_for_each_buffer(&c->scene->node, …)` twice — first to find
   the subtree's buffer-node bounding box (root surface + subsurfaces + popups;
   `wlr_scene_rect` border/focus-ring nodes are excluded, which is intended —
   chrome isn't papered), then to composite. Bounds/size clamped by
   `window_shader_math.h`.
2. Per-client state (two swapchains + the held buffer) lives in a small
   module-internal registry keyed by `Client *` (a linked list), because there
   is no `Client` field to hang it on (kalin.h is out of scope here). `raw` =
   `ARGB8888` (falls back to `XRGB8888`) so window translucency survives the
   round-trip; `shaded` likewise.
3. `composite_subtree()`: `wlr_renderer_begin_buffer_pass(raw)` → clear to
   transparent (`add_rect`, blend NONE) → `add_texture` each buffer node with its
   `src_box`/`dst`/`transform`/`opacity`, positioned relative to the window
   origin → submit. This is a normal wlroots pass (wlroots manages its own GL
   context here).
4. `paper_pass()`: raw-GLES2, same shape as `gl_pass()` — `wlr_texture_from_buffer`
   the `raw` buffer → sample with the paper program into the `shaded` buffer's
   FBO, setting `u_strength`/`u_paper`/`u_ink`/`u_preserve` + `tex`, under the
   saved/restored EGL context. Two buffers because a fragment pass can't read and
   write the same texture.
5. Return `shaded`; double-buffered across frames via the swapchains (the
   previous frame's held buffer is unlocked when the next is returned).

## Not verified / known Phase 0 caveats

- **Never run on a real GPU.** Exit criterion is *byte-identical output* — a
  pixel claim only a GLES2 hardware run can settle. The [[test-vm]] likely falls
  back to pixman (then `wlr_renderer_is_gles2` is false and the pass self-
  disables), so the VM can't prove it either. Needs a live-host run with
  `WLR_RENDERER=gles2` and the flag flipped.
- **Vertical flip is the #1 suspected first-run bug** — the shared fullscreen
  quad (`quad_uv`) maps clip bottom→texture top on the assumption wlroots
  textures are top-left origin; unverified. **Both** `gl_pass()` (output) and
  `paper_pass()` (window) use this same quad, so the window path inherits the
  exact same risk — a fix at the live gate applies to both. For the window
  path the net orientation is what matters: `composite_subtree` (wlroots pass)
  and the caller's `wlr_scene_buffer` sampling (wlroots) are both native, so
  only `paper_pass`'s single raw-GL hop can introduce a flip; check whether the
  reinjected window comes out upright.
- **Per-window path is entirely unexercised at runtime** — nothing calls
  `shaders_window_shade()` yet (that's `paper-window-bind`), and pixman VMs
  self-disable `paper_available`, so it has only been *compiled*, plus its pure
  helpers unit-tested. No claim that paper mode renders correctly.
- The per-client registry is keyed by `Client *`; `shaders_window_release()`
  must be called on unmap/destroy or the swapchains/held buffer leak. That call
  site is `paper-window-bind`'s responsibility.
- Bypasses `wlr_scene`'s per-frame damage/timer, so the output pass re-composites
  full-frame every frame (fine for proof; efficiency is later).
- Per-frame `wlr_texture_from_buffer` on the offscreen buffer is uncached.
- The `shaders_dir` default "shaders" is CWD-relative — fine from the repo root
  in dev; a real install needs the [[build-system]] to install it to a known
  path (not yet done).

## Next

- Phase 0 exit: flip `shaders_output_enabled = 1`, run on the host with
  `WLR_RENDERER=gles2`, confirm passthrough is visually identical (fix the flip
  if not). Then Phase 1 (camera color/vignette pass) per [[shaders]].
- Per-window paper mode: `paper-window-bind` drives `shaders_window_shade()` /
  `shaders_window_release()` from `dwl.c` (toggle bind + window rule, scene-node
  reinjection, teardown on unmap). Keeper follow-ups noted above: split the
  window path into its own TU + wire `window_shader_math_test.c` into
  `make test-unit` (both were blocked by Makefile being out of this task's
  scope), and GPU-verify the flip/orientation on a live host.

See also: [[scene-graph]] · [[wlroots]] · [[multi-camera]] · [[buffer-scaling]] ·
[[build-system]] · [[test-vm]] · [[shaders|plan/shaders]]
