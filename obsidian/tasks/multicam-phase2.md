# Task: multicam-phase2

- Owner: fleet worker (worktree-isolated).
- Objective: [[multi-camera]] Phase 2 — cross-monitor window moves. Three pieces,
  decided 2026-07-15 (see the Model section of `implementation/multi-camera.md`):
  1. **Drag hand-off**: Super+drag crossing the physical monitor boundary
     reassigns `c->mon` to the monitor under the cursor and re-bases the window
     under the cursor through the *new* monitor's camera (no visual jump).
  2. **Send-to-monitor bind**: explicit keybind teleporting the focused window
     to the other monitor's camera center (reassign `c->mon`, place at the
     target camera's viewport center).
  3. **Cross-camera edge severing**: on hand-off (drag or send), sever any
     [[connection-graph]] edges between the moved window and windows held by a
     different monitor — a line drawn through two different cameras is
     meaningless. `sever_connection(uint32_t, uint32_t)` in `kalin.h:751`
     already exists.
- Scope (may edit only):
  - `code/src/dwl.c` (interactive move path in `motionnotify()`/button-release; this task is the batch's single dwl.c owner)
  - `code/include/kalin.h` (new `ACT_*` enum value etc.)
  - `code/include/binds.h`, `code/src/modules/binds/bind_actions.c`, `code/config/default_binds.h`, `code/config/config.def.h`, `code/config/config.h` (the bind-DSL chain for the new action)
  - `code/src/modules/layout/connection_graph.c` (only if severing needs a helper beyond `sever_connection`)
  - `obsidian/implementation/multi-camera.md`, `obsidian/implementation/connection-graph.md`, `obsidian/implementation/keybindings.md` (impl notes)
  - `obsidian/agents/multicam-phase2/` (report zone)
- impl-note: `obsidian/implementation/multi-camera.md` (mark Phase 2 done +
  describe as-built), plus `keybindings.md` (new bind) and `connection-graph.md`
  (severing rule) if touched.
- Status: for-review
- Branch: worktree-agent-a35364366caf8dd02
- Why: [[roadmap]] "In progress" — [[multi-camera]] Phase 2; Phase 1 (core)
  landed 2026-07-15.

## Constraints
- Phase 1's model binds: every transform goes through the client's holder
  (`c->mon`) or `selmon`. Docked/fullscreen/maximized clients keep their camera
  bypass — hand-off must not break that.
- Re-basing math: on hand-off the window's *world* position changes so that its
  *screen* position under the cursor is continuous through the new camera
  (`screen_x = (world_x - m->cam.x) * m->cam.zoom + m->m.x`).
- Keybinds land via the bind DSL (`binds.h` / `bind_actions.c` /
  `default_binds.h`), not hardcoded in config.h — same pattern as `toggle-paper`.
- Single-monitor behavior must be byte-identical (no regression); verify unit
  tests stay green. Nested dual-output GPU verification (`WLR_WL_OUTPUTS=2`) is
  keeper-only — do NOT run it; note it as pending in your report.
