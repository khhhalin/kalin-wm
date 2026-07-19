---
status: merged
engine: claude
objective: Fix the compositor crash when the cursor hovers the screenshot-ui info panel (NULL-guard in xytonode + input-reject callbacks), and add fade-on-approach for the info panel.
scope:
  - code/src/dwl.c
  - code/src/modules/screenshot/screenshot_ui.c
  - obsidian/implementation/screenshot-ui.md
  - obsidian/agents/screenshot-crash-fade/
---

# Task: screenshot-crash-fade

- Owner: fleet worker (worktree-isolated). Brief: `obsidian/agents/screenshot-crash-fade/brief.md`.
- Objective: **stability crash** — hovering the [[screenshot-ui]] info panel
  segfaults the compositor because `xytonode()` (`dwl.c`) dereferences
  `wlr_scene_surface_try_from_buffer(...)->surface` on a plain pixel buffer that
  returns NULL. Mandatory: NULL-guard in `xytonode()` + `point_accepts_input`
  reject callbacks on the decorative buffers (paper-mode precedent). Plus the
  requested UX: fade the info panel by cursor distance
  (`wlr_scene_buffer_set_opacity()`, alpha floor ~0.15) via a per-motion hook —
  the fade does NOT fix the crash, part 1 is required independently.
- Priority: user-requested; front of the dwl.c queue. This is the sole active
  dwl.c owner (proto-toplevel-icon merged 2026-07-19, releasing the file).
- Spec: `obsidian/implementation/screenshot-ui.md` "Hover crash +
  fade-on-approach" section names every touch site.
- **Gate caveat:** the live primary checkout has pre-existing *uncommitted*
  `wlr_log` DBG prints in `code/src/dwl.c` (noted in the proto-toplevel-icon
  task) unrelated to this work. The worker branches from `HEAD` and won't see
  them; at the gate, git should surface any overlap as a conflict rather than
  silently combining or discarding those debug prints.
- Sequenced behind (dwl.c queue, one at a time): tmux-simplification (ASAP),
  then bar-over-ring layering fix.
- Why: [[roadmap]] known bug — a compositor crash, the #1 [[stability]] priority.
