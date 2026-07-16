# Task: session-resurrection

- Owner: fleet worker (worktree-isolated).
- Objective: extend [[persistence]] so saved clients' *processes* are respawned
  at compositor startup — today only geometry/connections are restored and the
  user reopens apps by hand (roadmap "Session resurrection", requested
  2026-07-16; design intent in `plan/persistent-desktop.md` Phase 2).
- Scope (may edit only):
  - `code/src/persistence.c`
  - `code/include/persistence.h`
  - `obsidian/implementation/persistence.md` (its impl note)
  - `obsidian/agents/session-resurrection/` (report zone)
  - Explicitly out of scope: `code/src/dwl.c`, `code/include/kalin.h`
    (owned by another active worker) — integration call sites go in the
    report for the keeper to wire at the gate.
- impl-note: `obsidian/implementation/persistence.md`.
- Status: for-review
- Branch: `worktree-agent-ab85fcd19538e6071`

## Known complications (scoped up front)
- **foot-server /proc-cmdline trap:** a terminal window's Wayland client is
  the foot *daemon* (`foot --server`) — respawning that cmdline recreates
  zero windows; terminals need the tmux-session-aware path (`foot -e
  kalin-term`).
- **tmux-based spawning:** every spawn is a `tmux new-window -t kalin-apps`
  ([[spawn]], since 2026-07-12) — the compositor never knows the launched
  command, and respawn must go through the same tmux session so apps inherit
  its captured `WAYLAND_DISPLAY`.
- **Bar-panel skipping:** `kalin-*-panel-*` clients are respawned by
  DockedPanel itself — resurrecting them here would double them.
- **Instance ordering:** saved `instance` indices only match if respawn order
  reproduces last run's spawn order (per-appid).
- **Flat-JSON parser limitation:** the hand-rolled parser brace-scans; the
  stored command must be one flat string with no raw `{}[]`.

## Note
This task note was missing from the repo when the worker started (the fleet
prompt referenced it, but it existed neither in the worktree nor on `main`);
it was reconstructed by the worker from `plan/roadmap.md` and
`plan/persistent-desktop.md`.
