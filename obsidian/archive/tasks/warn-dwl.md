# Task: warn-dwl

- Owner: fleet worker (worktree-isolated).
- Objective: clear all compiler warnings in `code/src/dwl.c`.
- Scope (may edit only): `code/src/dwl.c`.
- Status: merged.
- Branch: worker's own worktree branch (reports the name back).
- Why: [[agent-workflow]] work rules — defensive C, no dead code, fix warnings; touches the [[stability]] priority.

## Warnings to fix
- `collect_component` used but never defined — CONFIRMED (two independent worker runs) to be a **stray duplicate `static int collect_component(...)` forward declaration**; the real definition is `extern` in `code/src/modules/layout/connection_graph.c` and dwl.c already carries the correct `extern` decl. Fix = delete the stray `static` line.
- `-Wshadow` on `dx`/`dy` — in `motionnotify()`'s `CurMove` block, local `int dx, dy` shadow the function's `double dx, dy` params; rename the locals (e.g. `move_dx`/`move_dy`).
- Note: on the current HEAD there is **no** `-Wfloat-conversion` in dwl.c (those live in the shared `code/include/kalin.h`, a separate out-of-scope task).

## Notes
- `dwl.c` is the hot shared file — this is the only task this batch allowed to touch it.
