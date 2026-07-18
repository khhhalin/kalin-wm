# Persistent, terminal-first desktop (design intent)

**Goal:** the working environment survives a compositor restart with nothing
lost. kalin-wm is restarted constantly during dev (see [[dev-restart]]), so a
window/session that dies with the compositor is a real cost.

## The spectrum (why Level 2)

- **Level 3 — live GUI persistence** (GUI *processes* survive a restart) is not
  attainable *for free*: a Wayland client is bound to one compositor instance
  and quits on disconnect. The whole-desktop nested compositor (kills the
  per-window [[infinite-canvas]] model) and the persistent server-core split (a
  from-scratch rewrite) are both **rejected**. **But a third path was opened
  2026-07-18** — a **per-app stable Wayland proxy** (a waypipe fork that
  survives compositor death and replays each app's protocol state to the new
  instance), with apps in podman containers. It is the one mechanism that could
  reach Level 3 on this Intel box (CRIU/checkpoint is out — no Intel GPU
  plugin). Design, mechanics, and open decisions in
  [[podman-persistence]]; **investigation only, not committed to build.**
- **Level 2 — chosen.** Terminal *content* genuinely persists (its processes
  live in a tmux server, shown through disposable foot viewports); GUI windows
  auto-relaunch where they were and restore their own state.

## Simplification decided (2026-07-18) — untangle the two tmux jobs

The current setup conflates **two unrelated jobs**, which is why it feels heavy:
(1) a universal `kalin-apps` tmux *supervisor* wrapping every `spawn()`, making
tmux a hard launch dependency and forcing the double-nested `unset TMUX` hack;
(2) per-terminal tmux *content persistence* (`kalin-term`). Decision: separate
them and shrink both.

- **Kill the universal supervisor — `spawn()` execs argv directly**
  (the long-planned "spawn-direct", now decided). The compositor's own env
  already carries `$WAYLAND_DISPLAY`/`$KALIN_IPC_SOCKET`, so a direct `execvp`
  reaches this compositor without tmux. **tmux stops being a dependency for
  launching anything.** Two things that rode on the wrapper must move off it:
  - **Launcher toggle** (`ACT_TOGGLE_LAUNCHER`) currently tracks the launcher by
    tmux window name (`kalin-apps:launcher`); it needs a non-tmux tracking
    mechanism (e.g. a tracked pid/appid, like the pre-2026-07-12 path did).
  - **Respawn** (`persistence_respawn_saved()`) currently goes through
    `tmux new-window -t kalin-apps`; it forks+execs directly with the *new*
    compositor's env instead.
- **Terminals: ephemeral by default, persistence opt-in.**
  `Super+T` → bare `foot` (scratch: no tmux, dies with its window).
  `Super+Ctrl+T` → `foot -e kalin-term` (the persistent per-terminal tmux
  session, kept for when you actually want scrollback/processes to survive).
  The `kalin-tmux.service` + `kalin-term` wrappers **stay**, but now serve only
  the opt-in path, not every terminal.
- **Persistence: a curated appid allowlist, not everything.** Only a named few
  apps (e.g. browser, editor) are **auto-respawned** on restart;
  **terminals are never respawned**. Non-allowlisted apps keep the *passive*
  layout restore ([[persistence]] re-places them by appid+instance whenever they
  happen to re-map), they're just not relaunched for you. This replaces
  session-resurrection's current "respawn every saved app" behavior — the thing
  cluttering restores today. The allowlist is config-driven; exact list TBD.
- Consistent with the shelved [[podman-persistence]] direction (curated set,
  mixed with respawn-fresh) — this is the Level-2 version of the same "only a
  few apps deserve persistence" principle.

## What already exists

- `spawn()` (`dwl.c`) launches every app as `tmux new-window -t kalin-apps --
  <cmd>` — process supervision under the persistent tmux server (but not content
  persistence, and it double-nests an inner tmux — see spawn-direct below).
- [[persistence|persistence.c]] already saves/restores canvas layout
  (appid/title/instance, pos/size/crop/fullscreen, connection-graph edges) and
  re-applies it when a matching app re-maps.
- Binds hot-reload from `~/.config/kalin-wm/binds.conf`.

## Phase 1 — SHIPPED (2026-07-15, in home-config + binds.conf, no compositor change)

- `kalin-tmux.service` (systemd **user** service, `home-config/kalin-tmux.nix`):
  guarantees the tmux server + base `kalin-apps` session exist at login,
  independent of the compositor. Non-destructive (`has-session || new-session`,
  no `kill-server`).
- `kalin-term` / `kalin-term-pick` wrappers (`home-config/desktop.nix`):
  attach-or-create a persistent per-terminal tmux session; a fuzzel picker
  reattaches an existing one. `kalin-term` does `unset TMUX` so it works as a
  sibling session even while launched inside the `kalin-apps` supervisor window.
- Binds (`binds.conf`): `Super+t` → `foot -e kalin-term` (fresh session),
  `Super+Ctrl+t` → `kalin-term-pick`, `Super+grave` scratchpad →
  `... -e kalin-term scratch`. Bar TUIs / clip-picker stay ephemeral.

## Phase 2 — auto-relaunch SHIPPED 2026-07-17; now being narrowed + spawn-direct promoted

- **spawn-direct — now the decided direction** (see "Simplification decided"
  above), no longer just an option: `spawn()` execs argv directly, the universal
  `kalin-apps` wrapper is removed, and the `Super+T` (scratch) / `Super+Ctrl+T`
  (persistent) / scratchpad binds fold into `default_binds.h`.
- **Auto-relaunch (Level 2 for GUI) — SHIPPED 2026-07-17, being narrowed to a
  curated allowlist** (fleet task `session-resurrection`): at map, the client's
  real PID via `wl_client_get_credentials()` + `/proc/<pid>/cmdline` captures its
  relaunch command; stored flat in `SavedClientState.cmd` ([[persistence]]); at
  startup `persistence_respawn_saved()` (called from `run()`) replays each saved
  app once (deduped, capped at 32, ascending saved-instance order), letting
  `persistence_register_client()` re-place it. **Change decided 2026-07-18:**
  gate respawn by a config appid allowlist and stop respawning terminals (they
  become scratch by default) — the current unconditional replay is what clutters
  restores. The foot-server→`foot -e kalin-term` substitution becomes moot once
  terminals aren't respawned. Live end-to-end restart still unverified.
- Startup replay under kalinwm `-s` — subsumed by the above (replay is
  unconditional at startup, not `-s`-gated).

## Honest limits

- argv relaunch is best-effort (`/proc/cmdline`); apps with volatile args fall
  back to layout-only restore.
- GUI in-memory state is not preserved (Level 3) — only what the app itself
  restores (browser session, editor swap).

See also: [[dev-restart]] · [[persistence]] · [[keybindings]] · [[nixos-session]]
