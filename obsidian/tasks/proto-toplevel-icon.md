---
status: for-review
engine: claude
objective: Implement xdg-toplevel-icon-v1 (+ xdg-system-bell-v1) as new modules under code/src/modules/protocols/; dwl.c gets only one-line setup() registration.
scope:
  - code/src/modules/protocols/toplevel_icon.c
  - code/src/modules/protocols/system_bell.c
  - Makefile
  - code/src/dwl.c
  - obsidian/implementation/protocols.md
  - obsidian/agents/proto-toplevel-icon/
---

# Task: proto-toplevel-icon

- Owner: fleet worker (worktree-isolated). Brief: `obsidian/agents/proto-toplevel-icon/brief.md`.
- Objective: close the top of the [[protocols]] gap — `xdg-toplevel-icon-v1`
  (our own log warns about it every session) plus the trivial
  `xdg-system-bell-v1`, both via wlroots 0.20 wrappers, as modules under
  `code/src/modules/protocols/` per the [[roadmap]] rule.
- dwl.c ownership: this is the batch's **single dwl.c owner**, and its dwl.c
  diff is capped at the one-line `*_init()`/`wlr_*_create()` calls in `setup()`
  (+ forward decls). Keeper note: the live tree has uncommitted dwl.c edits —
  gate merge should stay trivial at this diff size.
- Out of scope by design (follow-up tasks): wiring icons into
  `foreign_toplevel.c` / the shell taskbar; any `Client`/`kalin.h` struct field.
- Why: [[roadmap]] "protocols" item names this protocol first; also the icon
  source the [[quickshell-shell]] taskbar currently lacks.
- **Manually unblocked, 2026-07-19**: the worker finished (report + 25/25
  unit tests green) and committed to `fleet/proto-toplevel-icon`, but META
  never committed this task note before dispatch — the worktree snapshot
  was cut without it, so the worker correctly refused to create it itself
  (`obsidian/tasks/` is out of its scope) and could not flip to for-review.
  Flipped here by hand so the gate can run normally. Root cause fixed in
  fleet-deck: dispatch now commits the task note/brief before cutting a
  worktree. **Caveat for the gate: the live checkout has pre-existing
  uncommitted debug prints in `code/src/dwl.c`** (unrelated `wlr_log` DBG
  calls) that are not part of this task — git should refuse the merge
  cleanly if they conflict rather than combine them; do not discard them.
