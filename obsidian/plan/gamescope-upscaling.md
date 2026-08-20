# Gamescope → kalin-wm (upscaling & what actually ports)

Design note (2026-08-20). Kalin asked what gamescope does and which of its optimizations
are worth stealing for kalin-wm. Answer: **the FSR/NIS upscalers are algorithm-portable and
map cleanly onto [[zoom]] via the existing [[shaders]] output pass; almost everything else
that gives gamescope its reputation is bound to its single-app + KMS-master model and either
doesn't fit an infinite-canvas WM or is already handled by wlroots.** No code yet — this is
the map and a phased proposal. Source-grounded against `ValveSoftware/gamescope@master`.

## What gamescope actually is (grounded)

Its own compositor — `steamcompmgr` (an X11-model composite manager) + a Wayland server; it
vendors *pieces* of wlroots but does **not** use wlroots' renderer/scene-graph. Pluggable
backends (`src/Backends/`): DRM/KMS (embedded, direct scanout), SDL/Wayland (nested),
headless, OpenVR. Model: **one primary app (the game) as layer 0 + a bounded handful of
overlay layers**. Compositing is **Vulkan compute-only** (`cs_*.comp`, no graphics pipeline
in the composite path). The whole thing is optimized around "render the game at a low internal
res, upscale to the panel" — that's the headline feature.

## Portability table — gamescope optimization → kalin-wm verdict

kalin-wm is a **real DRM session compositor** (dwl fork), so unlike a *nested* compositor it
already holds KMS master. That matters: the "KMS-bound" rows below are not out of reach for
kalin-wm the way they are for a nested app — the real barrier is kalin-wm's **multi-window +
zoom** model, not KMS access.

| gamescope optimization | source | portable to kalin-wm? | why |
|---|---|---|---|
| **FSR (EASU+RCAS) upscale** | `cs_easu.comp`, `cs_composite_rcas.comp` (GLSL calling AMD `ffx_fsr1.h`) | **YES — algorithm-portable** | Resolution-independent GLSL math; known GLES ports. Maps onto [[zoom]]-in. The prize — see below. |
| **NIS upscale** | `cs_nis.comp` (NVIDIA `NIS_Scaler.h`) | YES — algorithm-portable | GLSL/GLES variants exist; coeffs as textures. Alternative to FSR; FSR is the simpler first target. |
| **Integer / nearest / fit / fill / stretch scaling** | `steamcompmgr.cpp calc_scale_factor_scaler` | YES — trivial | Pure geometry + sampler-filter choice. kalin-wm could expose an integer/nearest zoom mode via the wlr_scene filter mode. Cheap, minor. |
| **Compute-only single-pass compositor** | `cs_composite_blit.comp` | Not worth it | Conceptually portable to a GLES2 fragment shader, but kalin-wm's wlr_scene compositing is fine; no reason to rewrite. |
| **DRM direct plane offload / zero-copy scanout** | `Backends/DRMBackend.cpp` + libliftoff (`drm_prepare_liftoff`) | Mostly NO | Needs a bounded layer set that fits hardware planes; a zoomed/arbitrary-canvas window can't scan out. wlroots **already** does direct scanout for a fullscreen unoccluded surface (the one case that fits) — kalin-wm gets that for free. The aggressive multi-plane liftoff doesn't fit the canvas model. |
| **Frame-pacing / late-latch (redzone)** | `vblankmanager.cpp` | Low priority | Portable in principle but needs accurate next-vblank prediction; wlroots has its own presentation timing. Big change for marginal latency. (Note: the "async-compute overlaps game render" framing is a myth — the real mechanism is adaptive late wakeup scheduling.) |
| **Framelimit focused/unfocused** (`-r`/`-o`) | `main.cpp`, `steamcompmgr.cpp` | Maybe, modest | kalin-wm could throttle re-render of off-screen/unfocused windows — [[shaders]] already flags iGPU fill-rate as the real risk. Independent of gamescope's code. |
| **VRR / HDR** | KMS `VRR_ENABLED` / `HDR_OUTPUT_METADATA` props; color math in `color_helpers.cpp` | Available via wlroots, not via gamescope | kalin-wm has KMS master, so these are achievable — but through wlroots' own adaptive-sync/HDR support, not by porting gamescope. Out of scope for this note. |

## The prize: FSR for zoom, as a camera/output effect

gamescope upscales because the game renders **below** output res. kalin-wm has the exact same
shape whenever you **zoom in** on the canvas: a window's buffer is at its native res but is
displayed larger — a low-res source magnified to a high-res footprint. Today [[zoom]] does that
magnification with a plain **bilinear** buffer scale (`client_set_buffer_scale()` →
`dest_size`, `dwl.c`), so zoomed-in content is soft. FSR is precisely the fix. (Zoom-*out*
is minification — FSR/EASU do not help there; that's a downscale-filter question, not this.)

Two things FSR does, and where each slots into kalin-wm's existing [[shaders]] pipeline:

- **RCAS (sharpening), same-res in→out.** The [[shaders]] output pass
  (`shaders_render_output()` → `gl_pass(src, dst, w, h)`) already renders the whole scene into
  a per-monitor offscreen **at output res** and runs one fullscreen fragment shader before
  commit. That offscreen is exactly "already-upscaled, needs sharpening" — so an **RCAS pass in
  `gl_pass`** (replacing/chaining the current passthrough) is a near-drop-in. Gate it on
  `MON_ZOOM_SAFE(m) > 1` and scale strength with zoom via a uniform. This is `FsrRcasF` — plain
  GLSL, sharpness packed by `FsrRcasCon(sharpness/10.0)`. **Tier 1: highest value / lowest cost.**
- **EASU (edge-adaptive upscale), low-res in→high-res out.** True EASU needs the source at its
  *native* res, but by the time `gl_pass` runs the bilinear magnification is already baked into
  the offscreen. To get real EASU you must intercept the zoomed window **before** wlr_scene's
  scale: render that window to a **native-res offscreen**, then EASU-upscale it to its zoomed
  footprint (the per-window analogue of gamescope upscaling layer 0). This is a per-window path
  parallel to the existing per-window "paper" shader, and is more plumbing. **Tier 2: better
  edges, do after Tier 1 proves the value.**

Both are AMD's public FSR-1.0 math (spatial, single-frame, no temporal/ML data), MIT-licensed,
already ported to GLES elsewhere — a good fit for kalin-wm's GLES2-only shader subsystem.

## Phased proposal (fits [[shaders]] "camera/output effects")

1. **RCAS sharpen as an output effect.** Add an `rcas.frag` alongside the passthrough; add a
   zoom uniform to `gl_pass`; enable-flag + sharpness in `config.h`; gate on zoom > 1. Adds one
   row to the [[shaders]] effect catalog (camera/output level). Verify on a real zoomed Zen/text
   window (sharpness of small glyphs is the visible win). Ties into [[zoom-scale-overhaul]].
2. **Integer/nearest zoom mode** (optional, cheap): expose a wlr_scene filter-mode toggle for
   pixel-exact zoom — useful for pixel art / precise inspection.
3. **Per-window EASU** (later, if Tier 1 justifies it): native-res offscreen → EASU → zoomed
   footprint, per the per-window shader path. Bigger change; measure iGPU cost first.

## Risks / honesty

- **iGPU fill-rate** (laptop target) is the real cost — an extra fullscreen RCAS pass every
  frame the monitor is zoomed. Gate strictly on zoom > 1 and consider damage-aware skipping;
  [[shaders]] already notes inserted passes can defeat damage tracking.
- **EASU input problem** is fundamental: kalin-wm scales per-surface *inside* wlr_scene before
  composition, so true EASU can't just be bolted onto the output pass — it needs the per-window
  pre-scale offscreen. Don't promise EASU quality from a Tier-1 output-pass sharpen.
- **Source caveats carried from the research** (not blockers, just don't overclaim): gamescope's
  zero-copy dmabuf import was inferred not line-verified; the "async compute" latency story is a
  myth (it's late-latch scheduling); FSR's RCAS pass in gamescope *also* composites overlays —
  a kalin-wm port separates sharpen from composite (we sharpen an already-composited frame).

## Links
[[shaders]] · [[zoom]] · [[zoom-scale-overhaul]] · [[kalin-wm]] · [[roadmap]]
