# Task: cursor-bugs

- Owner: fleet worker (worktree-isolated). **Not yet dispatched.**
- Objective: fix the three known cursor bugs (found 2026-07-09, listed in
  [[roadmap]] "Known minor bugs"):
  1. Cursor icon isn't reset to the pointer focus's own preferred shape after a
     move/resize/pan interaction ends — forced back to "default" instead
     (upstream dwl-heritage `TODO` in `code/src/dwl.c`).
  2. Cursor image jumps to (0,0) after all monitors wake from DPMS/sleep
     (`FIXME` in the output-config-apply path); the attempted
     `wlr_cursor_move(cursor, NULL, 0, 0)` fix doesn't restore the position.
  3. Cursor's first on-screen position at compositor startup is a hacky
     warp-to-last-position rather than a clean initial placement (cosmetic).
- Scope (may edit only):
  - `code/src/dwl.c`
  - impl-note: none expected (bug fixes, no behavior-model change) — update
    [[roadmap]]'s known-bugs list is the *keeper's* job at fold-back.
  - `obsidian/agents/cursor-bugs/` (report zone)
- Status: todo — **blocked on [[multicam-phase2]]** (that task owns `dwl.c`;
  dispatch this one only after it merges).
- Branch: (worker reports back)
- Why: [[roadmap]] "Known minor bugs"; [[stability]]/daily-drive polish.
