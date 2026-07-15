# Persistent, terminal-first desktop (design intent)

**Goal:** the working environment survives a compositor restart with nothing
lost. kalin-wm is restarted constantly during dev (see [[dev-restart]]), so a
window/session that dies with the compositor is a real cost.

## The spectrum (why Level 2)

- **Level 3 — live GUI persistence** (GUI *processes* survive a restart) is not
  practically attainable: a Wayland client is bound to one compositor instance
  and quits on disconnect. It would need a whole-desktop nested compositor
  (kills the per-window [[infinite-canvas]] model) or splitting kalin-wm into a
  persistent server-core + restartable frontend (a from-scratch rewrite).
  **Rejected.**
- **Level 2 — chosen.** Terminal *content* genuinely persists (its processes
  live in a tmux server, shown through disposable foot viewports); GUI windows
  auto-relaunch where they were and restore their own state.

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

## Phase 2 — PLANNED (kalin-wm code; sequence AFTER `paper-window-bind` merges — dwl.c is single-owner)

- **spawn-direct:** a spawn variant that execs argv directly (no `kalin-apps`
  tmux wrapper) + a new bind action; point terminals at it to drop the redundant
  supervisor layer (removes the nesting that `unset TMUX` currently works
  around). Also fold the `Super+t`/`Super+Ctrl+t`/scratchpad binds into
  `default_binds.h` for the embedded default.
- **Auto-relaunch (Level 2 for GUI):** at map, read the client's real PID via
  `wl_client_get_credentials()` + `/proc/<pid>/cmdline` to capture its relaunch
  argv; store it in `SavedClientState` ([[persistence]]); on startup, replay each
  saved app once (dedup + throttle), letting `persistence_register_client()`
  re-place it. Terminal windows relaunch as `foot → kalin-term` and re-attach
  their surviving session; GUI apps relaunch fresh and restore their own state.
- Startup replay wired into kalinwm `-s` so a restart auto-restores the desktop
  (generalizes [[dev-restart]]'s manual `foot -e tmux a`).

## Honest limits

- argv relaunch is best-effort (`/proc/cmdline`); apps with volatile args fall
  back to layout-only restore.
- GUI in-memory state is not preserved (Level 3) — only what the app itself
  restores (browser session, editor swap).

See also: [[dev-restart]] · [[persistence]] · [[keybindings]] · [[nixos-session]]
