# Worker addendum (kalin-wm project specifics)

Project-specific guidance appended to every worker's brief. Pipeline
mechanics (signal protocol, permission modes, merge/push rules, report
format) are fleet-deck's and are injected automatically — do not add them
here.

- This codebase is defensive C in the suckless tradition (see
  [[agent-workflow]]): every pointer deref NULL-checked, every divisor
  non-zero, no dead code, no new dependencies without need. Match the
  surrounding code's naming, structure, comment density, and idioms.
- Worktree-safe verification: `make test-unit` and the full build
  `nix develop -c make clean all` — green before you start, green after.
  VM checks and anything touching the live session are keeper-only; never
  run them.
- Read `obsidian/plan/` for intent (read-only to you) and the
  `obsidian/implementation/` note for the subsystem you touch; update that
  note in the same task if you change behavior — a stale note is a bug.
- Comments and commit messages explain *why*, never *what*. Keep changes
  minimal and focused; don't reformat unrelated code.
