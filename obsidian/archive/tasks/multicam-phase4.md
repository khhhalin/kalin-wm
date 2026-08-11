---
status: merged
engine: claude
objective: Multi-camera Phase 4 — per-monitor off-screen indicators, per-monitor wallpaper parallax, overview polish, and the Phase-4 vault doc sweep.
scope:
  - code/src/modules/ui/offscreen_indicators.c
  - code/src/modules/ui/wallpaper.c
  - code/src/modules/viewport/overview.c
  - obsidian/implementation/multi-camera.md
  - obsidian/implementation/wallpaper.md
  - obsidian/implementation/off-screen-indicators.md
  - obsidian/implementation/overview-mode.md
  - obsidian/implementation/viewport.md
  - obsidian/implementation/infinite-canvas.md
  - obsidian/implementation/world-coordinates.md
  - obsidian/agents/multicam-phase4/
---

# Task: multicam-phase4

- Owner: fleet worker (worktree-isolated). Brief: `obsidian/agents/multicam-phase4/brief.md`.
- Objective: [[multi-camera]] Phase 4 per its implementation note's Touch list —
  off-screen indicators per monitor (that monitor's windows against its camera,
  clamped to `m->m`), wallpaper parallax per monitor's own camera, overview
  per-monitor polish, and the Phase-4 vault updates ([[viewport]],
  [[infinite-canvas]], [[world-coordinates]]).
- Hard boundary: does NOT own `code/src/dwl.c` (proto-toplevel-icon does) or
  `code/include/kalin.h`. If per-monitor wallpaper needs a dwl.c/kalin.h seam
  (the `Wallpaper` global lives there), the worker delivers the analysis in its
  report instead of the edit.
- Phase 3 (shell-side keying of the `viewports` array) is intentionally NOT in
  this task — it lives in the separate quickshell repo, outside fleet worktrees;
  keeper-side work.
- Why: [[roadmap]] "In progress" — multi-camera Phases 3–4 are the remaining
  in-repo work after Phase 2 landed 2026-07-17.
