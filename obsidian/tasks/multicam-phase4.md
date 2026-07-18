# Task: multicam-phase4

- Owner: fleet worker (worktree-isolated).
- Objective: [[multi-camera]] Phase 4 — remaining per-monitor UI follows each
  monitor's own camera: off-screen indicators per monitor, wallpaper parallax
  per monitor, overview per-monitor polish, plus the Phase-4 vault sweep.
- Scope (may edit only):
  - `code/src/modules/ui/offscreen_indicators.c`
  - `code/src/modules/ui/wallpaper.c`
  - `code/src/modules/viewport/overview.c`
  - `obsidian/implementation/`: `multi-camera.md`, `wallpaper.md`,
    `off-screen-indicators.md`, `overview-mode.md`, plus the doc sweep of
    `viewport.md`, `infinite-canvas.md`, `world-coordinates.md`
  - `obsidian/agents/multicam-phase4/` (report zone)
- Status: for-review. Build green, all unit tests pass. Indicators + overview
  shipped; wallpaper delivered as seam analysis (true per-monitor parallax is
  impossible with unclipped rect-tile trees — needs the texture/scene-buffer
  design plus keeper-level `kalin.h`/dwl.c cleanup; see [[wallpaper]]).
  Dual-output GPU verification pending — keeper-only at the gate.
- Branch: fleet/multicam-phase4
- Why: [[roadmap]] — [[multi-camera]] Phase 4; Phases 1–2 landed 2026-07-15/17.
- Note: this task note was absent from `obsidian/tasks/` at dispatch (the brief
  referenced it); written by the worker at completion to keep the status board
  whole.

## Constraints
- No edits to `dwl.c` (owned by another active task) or `kalin.h`
  (keeper-level) — seam needs documented in the report instead.
- Camera-bypassed clients (docked/fullscreen/maximized) keep their bypass.
- VM / dual-output GPU verification is keeper-only.
