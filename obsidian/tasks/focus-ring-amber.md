---
status: merged
engine: claude
objective: Re-theme focuscolor from inherited dwl teal-blue (0x005577ff) to warm amber (0xf0a030ff) in both config.def.h and config.h; config-only.
scope:
  - code/config/config.def.h
  - code/config/config.h
  - obsidian/agents/focus-ring-amber/
---

# Task: focus-ring-amber

- Owner: fleet worker (worktree-isolated). Brief: `obsidian/agents/focus-ring-amber/brief.md`.
- Objective: apply the [[focus-ring]] color decision (2026-07-18) — swap
  `focuscolor` to `COLOR(0xf0a030ff)` (shell amber accent) from the inherited
  dwl `0x005577ff`, in both paired config files. See the impl note's "Color"
  section for the spec.
- Disjoint by design: config-only, owns no `dwl.c`/module/impl-note paths —
  clean of the two running dwl.c/module tasks (`proto-toplevel-icon`,
  `multicam-phase4`).
- Sequenced separately (NOT this task): the bar-over-ring **layering** fix in
  `focus-ring.md` is a `dwl.c` scene-layer change; it queues behind the active
  dwl.c owner (`proto-toplevel-icon`) per the [[fleet-workflow]] one-dwl.c-owner
  rule, and is not dispatched while that task is running.
- Why: [[focus-ring]] canon — the focused ring was the last off-palette element
  (stock dwl teal) against the warm shell theme.
