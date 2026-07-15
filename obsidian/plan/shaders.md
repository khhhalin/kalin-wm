# Shaders (design intent)

- **Status: planned, not started (designed 2026-07-15).** Add GPU fragment
  shaders to [[kalin-wm]] at two levels: **per-window** (shadow, rounded corners,
  blur-behind, dim/focus, paper-mode reading tint) and **camera/output**
  (color-grade/CRT/vignette over the whole [[viewport]]).
- This note is design intent (keeper layer). The as-built record will live in
  `implementation/shaders.md` once building starts.

## The constraint that dictates the whole shape

- kalin-wm renders through **`wlr_scene_output_commit()`** (`dwl.c` `rendermon()`),
  the fully-managed [[scene-graph]] path, on a `wlr_renderer_autocreate` renderer.
  `wlr_scene` abstracts the renderer (GLES2 **or** Vulkan) and therefore exposes
  **no hook to run arbitrary GLSL on a node**. wlroots' `wlr_render_pass` is
  fixed-function (texture + matrix + alpha + filter), not programmable.
- Everything good rides on `wlr_scene`: [[zoom]]-as-true-scale,
  [[buffer-scaling]], damage tracking, direct scan-out, the [[multi-camera]]
  transform. Dropping it (the Hyprland answer — own GL renderer) is a multi-month
  rewrite that deletes all of the above. **Rejected.**
- **Chosen approach:** keep `wlr_scene` for the scene render; drop to **raw GLES2
  only at the compositing stage**. Render scene content to offscreen textures via
  `wlr_scene_output_build_state(... , &opts{ .buffer })`, then run our own GLSL
  programs (EGL context from the GLES2 renderer) into the target buffer. Bypass is
  confined to the final passes, not the scene.
- **Hard constraint: pin GLES2** (`WLR_RENDERER=gles2`, set in the
  [[nixos-session]] wrapper). Vulkan can't run our shaders — at startup, if the
  renderer isn't GLES2, log and **disable shaders gracefully** (compositor still
  runs). Same graceful-fallback on any shader compile error → passthrough.

## One infrastructure, both levels (per the "both together" decision)

New module `code/src/modules/shaders/`:
- `shader.c` — load/compile `.frag` files, uniform binding, graceful fallback.
- `render.c` — per-[[multi-camera|Monitor]] offscreen FBO/texture pool (create +
  resize with output/zoom), the GLES2 passes, EGL `make_current` handling.
- Hooks `rendermon()`: scene → offscreen → shader passes → output.

Per-frame pipeline per monitor:
```
scene subtree(s) → [per-window offscreen tex] → window shader → wlr_scene_buffer
whole scene      → [monitor offscreen tex]    → output shader  → real output
```

## Per-window effects decompose by difficulty (land incrementally)

Not all per-window effects need the window rendered to an offscreen texture:

- **Shadow — easiest.** A separate shader-drawn quad node *behind* the window
  (`c->scene` sibling). No window-texture round-trip. (Subsumes the roadmap
  [[stability|v1.0]] "window shadows" item.)
- **Rounded corners / dim-inactive / paper-mode — composite-time window shader.**
  These transform the *window's own* texture at composite: alpha-round the
  corners, multiply brightness, remap color. Needs the window's texture shaded
  and reinjected as a `wlr_scene_buffer`. Multi-surface windows (subsurfaces/
  popups) need their subtree rendered to the offscreen buffer, not just the root
  `wlr_surface`.
- **Blur-behind — hardest.** Samples the *canvas behind* the translucent window,
  i.e. the scene-composited-so-far as a texture (multi-tap gaussian/dual-kawase).
  This is *why* one shared design: the monitor offscreen texture / a background
  snapshot feeds the blur. Opt-in, cache when static.
- **Dim/focus — ties to [[focus-ring]] + [[connection-graph]].** Dim inactive,
  highlight focused; spawn/close dissolve could ride [[client-anim]].

## Camera / output effects

- Whole-scene → monitor-sized offscreen texture → one full-screen fragment pass →
  output. Color-grade/LUT, CRT scanlines, vignette, film grain, chromatic
  aberration. Per-Monitor (each [[multi-camera|camera]] owns its offscreen tex).

## Effect catalog (wanted)

| Effect | Level | Notes |
|---|---|---|
| Window shadow | window (behind-quad) | subsumes roadmap "window shadows" |
| Rounded corners | window (composite) | subsumes roadmap "rounded corners"; radius in screen px → scale with [[zoom]] |
| Dim inactive / focus | window (composite) | uniform driven by focus state |
| Paper mode | window (composite) | **white→warm-yellow reading tint**; per-window toggle (bind) or appid rule (e.g. browser reader). Color remap: near-white → paper, preserve darks/hue |
| Blur-behind | window (needs bg) | costliest; opt-in, cached |
| Color-grade / CRT / vignette | camera/output | whole [[viewport]] |

## Config model (external `.frag`, startup-loaded — per decision)

- `.frag` files ship in repo `shaders/`, installed to a known path by the Nix
  [[build-system]]; `config.h` lists effect→file + uniform defaults + enable
  flags; overridable via env/XDG path for iteration. Compiled at startup.
- Per-window assignment via **window rules (appid → effect set)**, plus runtime
  toggles (e.g. `Super+…` paper-mode on the focused window) — new [[keybindings]].
- Bad/missing file → log + disable that effect; never crash (defensive-C rule).

## Interactions / risks (honest)

- **Damage tracking** partly breaks: inserted offscreen passes may force
  re-render of shaded windows every frame even when idle (blur worst). Accept
  higher idle GPU or add per-effect dirty-tracking. Direct scan-out lost for
  shaded monitors/windows (fine for desktop).
- **[[zoom]]:** shaders run in screen space *after* the scene transform, so
  zoom-scale still works — but corner radius / shadow size / blur radius are
  screen-px and must take a **zoom uniform** or they distort when zoomed.
- **[[multi-camera]]:** infra is per-Monitor, like `Monitor.cam` — fits.
- **iGPU fill-rate:** blur-behind on many windows is the real perf risk on the
  laptop target. Opt-in + cache.

## Phasing (infra once; effects incremental)

0. **Infra** — GLES2 pin + graceful disable; offscreen FBO pool per Monitor;
   GLSL loader; raw-GLES2 pass plumbing in `rendermon()`. Prove with a no-op
   passthrough output shader (must be byte-identical to today).
1. **Camera output pass** — one full-screen shader (color-grade/vignette). Lowest
   risk, validates the offscreen→shade→output path end to end.
2. **Shadow** — behind-quad; subsumes v1.0 shadows item.
3. **Composite window shaders** — rounded corners, dim, **paper mode**.
4. **Blur-behind** — background snapshot + multi-tap; opt-in.
5. **CRT/grain/aberration**, focus/spawn dissolve polish; vault as-built note.

## Touch list (anticipated)

- `code/src/modules/shaders/{shader,render}.c` (new), `shaders/*.frag` (new).
- `dwl.c` `rendermon()` — route commit through the shader pipeline; GLES2 assert
  in `setup()`.
- `code/include/kalin.h` — per-Monitor offscreen state; per-Client shader flags.
- `config.h`/`config.def.h` — effect table, window-rule effect assignment.
- [[keybindings]] — paper-mode / effect toggles.
- [[nixos-session]] wrapper — `WLR_RENDERER=gles2`.
- [[build-system]] — install `shaders/` to a known path.

See also: [[scene-graph]] · [[wlroots]] · [[buffer-scaling]] · [[multi-camera]] ·
[[zoom]] · [[viewport]] · [[focus-ring]] · [[compile-time-config]] · [[roadmap]]
