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
  - **NOT** `code/src/dwl.c` and **NOT** `code/include/kalin.h` — both are owned
    by another active task. Keep all per-client bookkeeping inside persistence's
    own registry (it already keys entries by client pointer). Where an
    integration call into dwl.c is unavoidable (e.g. triggering respawn at
    startup after the tmux `kalin-apps` session is bootstrapped in `run()`),
    expose a clean function in `persistence.h` and specify the exact one-line
    call site in your report — the keeper wires it at the gate.
- impl-note: `obsidian/implementation/persistence.md`.
- Status: running
- Branch: (worker reports back)
- Why: [[roadmap]] "v1.0 features — open" — Session resurrection (requested
  2026-07-16).

## Known complications (scoped 2026-07-16, from the roadmap)
- **The /proc cmdline trap**: the Wayland client PID's `/proc/<pid>/cmdline` is
  the foot *server* (`foot --server`) for every terminal window — respawning it
  recreates zero windows. Read `implementation/spawn.md` first: since 2026-07-12
  every spawn is a `tmux new-window -t kalin-apps -n <name> -- <cmd>...` in the
  persistent `kalin-apps` tmux session. Consider whether the right identity to
  save is the tmux-window-level command rather than the client PID's cmdline —
  tmux's server outlives the compositor, which may make terminal windows
  recoverable via a tmux-session-aware path (`kalin-term`).
- **Skip bar panels**: clients with appid `kalin-*-panel-*` must not be
  respawned — DockedPanel respawns them itself.
- **Instance ordering**: saved `instance` indices only match if respawn order
  matches last spawn order — respawn same-appid clients in ascending saved
  `instance` order.
- **Flat JSON only**: the hand-rolled parser is non-nesting-safe — any new
  saved field must stay flat inside the existing `clients` objects (no nested
  objects/arrays). Bump `version` if the format changes incompatibly.
- Respawn must be best-effort and non-fatal: a saved command that no longer
  exists must not break startup.
