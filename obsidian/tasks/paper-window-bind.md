# Task: paper-window-bind

- Owner: fleet worker (worktree-isolated).
- Objective: add per-window paper-mode state and control — a `Client.paper_mode`
  flag, a `Super+…` keybind toggling it on the focused window, an optional appid
  window-rule, config-driven uniform defaults, and the per-window call into the
  `paper-shader-core` API so a flagged window is composited through `paper.frag`.
- Scope (may edit only):
  - `code/src/dwl.c` (the single hot-file owner for this feature — see [[fleet-workflow]])
  - `code/include/kalin.h` (Client struct field)
  - `config.def.h`, `config.h` (keybind + paper uniform defaults)
  - `obsidian/implementation/keybindings.md` (its impl note)
  - `obsidian/agents/paper-window-bind/` (report zone)
- impl-note: `obsidian/implementation/keybindings.md`.
- Status: blocked — do not dispatch until `paper-shader-core` is merged to main
  (this task calls its `shaders.h` API and will not compile without it).
- Branch: worker's own worktree branch (report it back).
- Why: [[shaders]] design intent — per-window paper-mode assignment via focused-window
  toggle + appid rule ([[keybindings]] new bind).

## Design constraints
- Depends on the merged `paper-shader-core` API in `shaders.h`. Call it from the
  client-render path for windows with `paper_mode` set; pass config-driven uniform
  values (`u_strength`/`u_paper`/`u_ink`/`u_preserve`).
- Keybind: a new `Super+…` chord toggling `paper_mode` on the focused client. Pick a
  free chord (grep `config.def.h` bindings first; document the choice).
- Appid rule: extend the existing window-rule mechanism (do not invent a new one) so
  an appid can default to paper mode (e.g. a browser reader).
- Config defaults for the paper uniforms live in `config.def.h`/`config.h` — the
  starting values can be lifted from the tuned WebGL demo (paper `#f2e4c2`, ink
  `#2b2114`, strength ~0.92, preserve ~0.85) unless the keeper supplies final values.

## Verification (worktree-safe only)
- `nix develop -c make clean all` green (exit 0) before and after.
- `nix develop -c make test-unit` all pass.
- **You cannot GPU-verify the visual effect** (pixman fallback). Verify only that the
  toggle/state/rule logic is correct and it builds; the keeper GPU-verifies live.

## Notes
- Defensive C, suckless style. Keep `dwl.c` edits minimal and localized. Watch the
  `kalin.h`-vs-dwl.c-own-`Client` duplication ([[compile-time-config]]) — if both
  Client definitions need the field, this task owns both (they're one struct concern).
