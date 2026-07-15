# shaders

- **Status: Phase 0 infrastructure in tree, gated OFF, NOT yet GPU-verified
  (2026-07-15).** GPU fragment-shader post-processing. Design intent + full
  phasing: [[shaders]] in `plan/`. This note is the as-built record.
- As-built so far is Phase 0 only: the offscreen-render + fragment-pass
  plumbing and a passthrough shader. No real effect exists yet.

## Where it lives

- `code/src/modules/shaders/shaders.c` — the module (separate TU, links dwl.c
  globals `drw`/`alloc` via [[compile-time-config|kalin.h]], like `capture.c`).
- `code/include/shaders.h` — opaque API (`shaders_init`, `shaders_render_output`,
  `shaders_output_destroy`); keeps the GL/EGL headers out of the rest of the tree.
- `shaders/passthrough.frag` — the Phase 0 shader (GLSL ES 1.00). The vertex
  stage is a built-in string in `shaders.c` (fixed fullscreen quad, not a
  user-authored effect).
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
  captures the EGL handles, compiles the program under a current context. Any
  failure → logs once, stays disabled, never aborts.

## Not verified / known Phase 0 caveats

- **Never run on a real GPU.** Exit criterion is *byte-identical output* — a
  pixel claim only a GLES2 hardware run can settle. The [[test-vm]] likely falls
  back to pixman (then `wlr_renderer_is_gles2` is false and the pass self-
  disables), so the VM can't prove it either. Needs a live-host run with
  `WLR_RENDERER=gles2` and the flag flipped.
- **Vertical flip is the #1 suspected first-run bug** — `gl_pass()` maps clip
  bottom→texture top on the assumption wlroots textures are top-left origin;
  unverified.
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

See also: [[scene-graph]] · [[wlroots]] · [[multi-camera]] · [[buffer-scaling]] ·
[[build-system]] · [[test-vm]] · [[shaders|plan/shaders]]
