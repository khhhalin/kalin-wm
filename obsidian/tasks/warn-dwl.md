# Task: warn-dwl

- Owner: fleet worker (worktree-isolated).
- Objective: clear all compiler warnings in `code/src/dwl.c`.
- Scope (may edit only): `code/src/dwl.c`.
- Status: todo.
- Branch: worker's own worktree branch (reports the name back).
- Why: [[agent-workflow]] work rules — defensive C, no dead code, fix warnings; touches the [[stability]] priority.

## Warnings to fix
- `dwl.c:524` — `collect_component` used but never defined. **Investigate first**: this is either a dead call to be removed or a missing/renamed definition — decide which, don't just silence it.
- The `-Wfloat-conversion` and `-Wshadow` warnings in this file — fix the narrowing/shadowing properly, not by casting away a real precision issue.

## Notes
- `dwl.c` is the hot shared file — this is the only task this batch allowed to touch it.
