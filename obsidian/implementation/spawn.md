# Spawn

- Spawn launches an external program from a [[keybindings|keybind]].
- **NOTE (decided 2026-07-18, not yet built):** the universal `kalin-apps` tmux
  wrapper described below is slated for removal — `spawn()` will `execvp` argv
  directly (tmux stops being a launch dependency). See the "Simplification
  decided" section of [[persistent-desktop]]. The description below is still the
  current as-built until that lands (sequences after the active dwl.c worker).
- **Every spawn becomes a tmux window** (2026-07-12): instead of `execvp`ing
  the command directly, kalin-wm forks and re-execs `tmux new-window -t
  kalin-apps -n <name> -- <cmd> <args...>` — the command runs as a window in
  a persistent `kalin-apps` tmux session instead of a bare child process.
  `tmux attach -t kalin-apps` shows any launched app's stdout/stderr live,
  and tmux's own window list / `kill-window` are the way to manage or close
  one, instead of the compositor tracking raw pids. The `kalin-apps` session
  is bootstrapped once at compositor startup (`run()`, `dwl.c`) — right
  after `$WAYLAND_DISPLAY`/`$KALIN_IPC_SOCKET` are set, so the tmux
  *server's* captured environment (reused for every window it ever creates,
  for as long as it stays alive — even across a compositor restart, since
  tmux's server is a separate long-lived daemon) lets every launched app
  actually reach this compositor. **Requires `tmux` on `$PATH`** — this is
  now a hard runtime dependency for launching anything, not just a
  workflow convenience.
- The tap-launcher (`ACT_TOGGLE_LAUNCHER`) is tracked by tmux window name
  (a fixed `"launcher"`) rather than a pid: toggling tries `tmux kill-window
  -t kalin-apps:launcher` first (synchronous, since the toggle needs to know
  whether it worked); if that fails (wasn't open), it spawns a new one
  instead. Replaced the old PTY-based `spawn_pid()`/`pty_register()`
  machinery (`modules/input/pty.c`, since removed) that existed only to let
  the old kill-by-pid-group close path work and to log stdout — both are
  superseded by tmux itself.

- Default spawns: `Super+T` → `foot` (terminal), `Super+P` → `fuzzel` (launcher), `Super+O` → toggle-overview ([[overview-mode]]).

- Spawn failures are reported (pipe-based error reporting / logging) — but
  now only catches `tmux` itself failing to start, not the wrapped command
  failing inside its window (that shows up as an error message in the tmux
  window itself, visible via `tmux attach`, not in the compositor's log).
- A historical crash when spawning a terminal with several windows open was traced to `resize()` touching an under-initialized client and fixed; see [[stability]].

## Placement (current — no [[column-layout]] any more)

- A new window's spawn-parent is whichever window was focused right before
  it was created (snapshotted in `mapnotify()` before the new client is
  focused or inserted into any list). Placed `SPAWN_GAP` px to the parent's
  right at the parent's baseline y, and **spliced into the [[rail]]** — the 1D
  scrolling order (layout Phase 2 DONE 2026-08-13). Any rail successors of the
  parent shift right to open room; closing a rail member shifts them back left
  (gap-close). The [[connection-graph]] that used to insert-into-a-line was
  removed in Phase 1; the rail is its 1D heir — see [[rail]] and [[layout-impl]].
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

## Placement grid (2026-07-25)

- Every **automatic** placement snaps to `placement_grid` (config, default 20 —
  the same pitch as `SPAWN_GAP`): the parent+gap spawn position, the
  cursor-centred fallback, the monitor-centre fallback, and the position
  restored from [[persistence]]. Drags are never snapped, so a hand-dropped
  window stays exactly where it was dropped.
- Why: windows drifted to arbitrary offsets. Two causes compounded —
  cursor-centred spawns land wherever the mouse happens to be, and a persisted
  position (which takes priority over the tidy parent+gap rule) preserved every
  past arbitrary coordinate forever. Snapping both ends normalises the canvas
  without giving up free positioning.
- `snap_grid()` in `dwl.c` rounds via `floorf(v/g + 0.5)` rather than integer
  division, so negative world coordinates round the same way as positive ones.
