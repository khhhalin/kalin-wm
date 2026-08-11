# Task: session-resurrection

- Owner: fleet worker (worktree-isolated).
- Objective: [[persistence]] currently restores geometry/connections but not the
  *processes* — after a compositor restart the user reopens every app by hand.
  Add session resurrection: save enough launch info per client to respawn saved
  clients at compositor startup, in an order that preserves `instance` matching.
- Scope (may edit only):
  - `code/src/persistence.c`
  - `code/include/persistence.h`
  - `obsidian/implementation/persistence.md` (impl note)
  - `obsidian/agents/session-resurrection/` (report zone)
  - Explicitly out of scope: `code/src/dwl.c`, `code/include/kalin.h` (owned by
    another active worker) — integration call sites go in the report for the
    keeper to wire at the gate.
- impl-note: `obsidian/implementation/persistence.md`.
- Status: merged (2026-07-17, merge commit 6ba6db3; keeper wired the single
  `persistence_respawn_saved()` call into `run()` at the gate — commit 8552627).
  Build green, all unit tests pass; worker's ad-hoc integration test (real
  `persistence.o` + fake tmux shim) verified capture/quoting/skip/ordering.
  **Live end-to-end restart unverified** — needs a real compositor restart.
- Branch: `worktree-agent-ab85fcd19538e6071`
- Why: [[roadmap]] "v1.0 features — open" — Session resurrection (requested
  2026-07-16); design intent also in `plan/persistent-desktop.md` Phase 2.

## Known complications (scoped 2026-07-16, from the roadmap)
- **The /proc cmdline trap**: the Wayland client PID's `/proc/<pid>/cmdline` is
  the foot *server* (`foot --server`) for every terminal window — respawning it
  recreates zero windows; terminals need the tmux-session-aware path
  (`foot -e kalin-term`).
- **tmux-based spawning**: every spawn is a `tmux new-window -t kalin-apps`
  ([[spawn]], since 2026-07-12) — the compositor never knows the launched
  command, and respawn must go through the same tmux session so apps inherit
  its captured `WAYLAND_DISPLAY`.
- **Skip bar panels**: clients with appid `kalin-*-panel-*` must not be
  respawned — DockedPanel respawns them itself.
- **Instance ordering**: saved `instance` indices only match if respawn order
  matches last spawn order — respawn same-appid clients in ascending saved
  `instance` order.
- **Flat JSON only**: the hand-rolled parser is non-nesting-safe — any new
  saved field must stay flat inside the existing `clients` objects (no nested
  objects/arrays, no raw `{}[]` in strings).
- Respawn must be best-effort and non-fatal: a saved command that no longer
  exists must not break startup.

## Note
The keeper's copy of this note was uncommitted on `main` when the worker's
worktree branched, so the worker reconstructed its own from `plan/roadmap.md`
and `plan/persistent-desktop.md` — the two copies agreed on the contract and
were reconciled at the gate (add/add merge).
