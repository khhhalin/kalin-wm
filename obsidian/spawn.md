# Spawn

- Spawn launches an external program from a [[keybindings|keybind]].
- kalin-wm forks and `execvp`s the command; the child inherits the compositor's environment, including `$KALIN_IPC_SOCKET` (see [[ipc-socket]]) and `$WAYLAND_DISPLAY`.

- Default spawns: `Super+T` → `foot` (terminal), `Super+P` → `fuzzel` (launcher), `Super+O` → toggle-overview ([[overview-mode]]).

- Spawn failures are reported (pipe-based error reporting / logging).
- A historical crash when spawning a terminal with several windows open was traced to `resize()` touching an under-initialized client and fixed; see [[stability]].

## Placement (current — no [[column-layout]] any more)

- A new window's spawn-parent is whichever window was focused right before
  it was created (snapshotted in `mapnotify()` before the new client is
  focused or inserted into any list). Placed `SPAWN_GAP` px to the parent's
  right and connected to it — see [[connection-graph]] for the full
  placement priority order and the insert-into-a-line splice when the
  parent already has an East neighbor.
- **No spawn-parent** (nothing was focused, or the focused window is on a
  different monitor): placed **under the cursor** (2026-07-09, `mapnotify()`
  in `code/src/dwl.c`), not the monitor's geometric center — `c->geom.x/y`
  set from `SCREEN_TO_WORLD_X/Y(cursor->x/y)` minus half the window's size,
  only when the cursor is actually on the new client's monitor
  (`xytomon(cursor->x, cursor->y) == c->mon`, matching the same-monitor
  guard the spawn-parent path uses). Falls back to monitor-center only when
  the cursor is on some *other* monitor. Persisted position (from a previous
  run of this exact app instance) still takes priority over both.
- A window with a real xdg-shell parent (a dialog / transient-for) inherits
  the parent's monitor and connects to that real parent instead of the
  focus-snapshot — a more natural choice for a genuine dialog.
