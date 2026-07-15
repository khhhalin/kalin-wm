# Task: paper-shader-core

- Owner: fleet worker (worktree-isolated).
- Objective: build the per-window composite-shader machinery — render a client's
  scene subtree to an offscreen buffer, run a `.frag` over it, reinject the shaded
  result as a `wlr_scene_buffer` — and wire `paper.frag`'s uniforms. Expose a clean
  API in `shaders.h` for dwl.c to drive. Do NOT touch dwl.c or config.
- Scope (may edit only):
  - `code/src/modules/shaders/` (add a new TU, e.g. `window_shader.c`; edit `shaders.c` only as needed for shared EGL/context helpers)
  - `code/include/shaders.h`
  - `shaders/paper.frag`
  - `obsidian/implementation/shaders.md` (its impl note)
  - `obsidian/agents/paper-shader-core/` (report zone)
- impl-note: `obsidian/implementation/shaders.md`.
- Status: running.
- Branch: worker's own worktree branch (report it back).
- Why: [[shaders]] design intent — per-window paper-mode reading tint as a
  composite-time window shader (render window subtree offscreen → shade → reinject).

## Design constraints (read plan/shaders.md first)
- Follow the **composite-time window shader** path in `plan/shaders.md` ("Per-window
  effects decompose by difficulty"): the window's own texture/subtree is rendered to
  an offscreen buffer, shaded, and reinjected as a `wlr_scene_buffer`. Multi-surface
  windows (subsurfaces/popups) need the whole `c->scene` subtree rendered, not just
  the root `wlr_surface`.
- Reuse the existing Phase 0 EGL/context handling in `shaders.c` (`shaders_init`
  captured the renderer's EGL handles). Same graceful-degradation contract: on
  Pixman/Vulkan or any compile/render failure, the window-shade path is a no-op and
  the caller falls back to the unshaded window — **never abort the compositor**.
- `paper.frag` uniforms to wire: `u_strength` (float), `u_paper` (vec3), `u_ink`
  (vec3), `u_preserve` (float). Bake sensible defaults into the API signature; the
  dwl.c/config wiring (a later task) will override them. Do not add config symbols.
- Suggested API shape (adjust as the offscreen model requires), e.g.:
  `bool shaders_window_effect(struct Client *c, ...uniform params...);` plus a
  teardown for per-window offscreen resources. Keep GL/EGL headers confined to the
  shaders module — the header stays opaque (forward-declare `struct Client`).

## Verification (worktree-safe only — see plan/fleet-workflow.md)
- `nix develop -c make clean all` green (exit 0) before and after.
- `nix develop -c make test-unit` all pass; add unit tests for any pure helper
  (e.g. offscreen-size/uniform-packing math) that can be tested without a GPU.
- **You cannot GPU-verify** — the VM/headless envs fall back to pixman, which
  self-disables the GLES2 path. State this plainly in your report; do not claim the
  effect renders correctly. The keeper GPU-verifies live.

## Notes
- Defensive C, suckless style: every pointer deref NULL-checked, every divisor
  non-zero, no dead code, no new deps. Match `shaders.c`'s existing idioms.
- The suspected Phase 0 vertical-flip issue (offscreen texture origin) is relevant
  here too — document your flip assumption in a comment so the keeper can check it at
  the live gate.
