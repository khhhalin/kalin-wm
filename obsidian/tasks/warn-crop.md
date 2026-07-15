# Task: warn-crop

- Owner: fleet worker (worktree-isolated).
- Objective: clear all compiler warnings in `code/src/modules/crop/crop_mode.c` (4 warnings).
- Scope (may edit only): `code/src/modules/crop/crop_mode.c`.
- Status: todo.
- Branch: worker's own worktree branch (reports the name back).
- Why: [[agent-workflow]] work rules — defensive C, fix warnings; [[crop-mode]] is the affected subsystem.

## Notes
- Fix each warning at its root (declaration order, narrowing, shadowing) in the suckless C90 style — don't add casts that hide a real bug.
