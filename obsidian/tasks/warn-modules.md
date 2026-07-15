# Task: warn-modules

- Owner: fleet worker (worktree-isolated).
- Objective: clear all compiler warnings in the remaining non-dwl, non-crop modules.
- Scope (may edit only): `code/src/modules/capture.c`, `code/src/modules/backlight.c`, `code/src/modules/ipc.c`, `code/src/crash_report.c`, `code/src/modules/input/keyboard.c`.
- Status: todo.
- Branch: worker's own worktree branch (reports the name back).
- Why: [[agent-workflow]] work rules — defensive C, no dead code, fix warnings.

## Warnings by file
- `capture.c` — 3 (declaration-after-statement).
- `backlight.c` — 2, incl. a `-Wformat-truncation` at `backlight.c:42` (`%s` may truncate into a 64-byte buffer). **Check the buffer size is actually adequate**, don't just silence it.
- `ipc.c` — 1 `-Wformat-truncation` at `ipc.c:683` (`%s` into a 108-byte region). Same: verify no real truncation before silencing.
- `crash_report.c` — 2 `-Wunused-macros` (`MAX_CRASH_REPORTS`, `CRASH_REPEAT_WINDOW_SECS`): remove the dead macros, or wire them in if they were meant to be used.
- `keyboard.c` — 1.

## Notes
- The two `format-truncation` ones are the only potentially-real bugs in this task — treat them as correctness, not lint.
