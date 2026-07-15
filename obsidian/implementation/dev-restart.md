# Dev-session live restart

- While kalin-wm is under active development, Kalin runs it directly (not via the packaged [[nixos-session]]) with the `kalinwm` dev-launcher script.
- `kalinwm` rebuilds `build/kalin-wm` if it is missing, then runs `kalin-wm -s 'qs & foot --server'`.
- Testing a fresh [[build-system|build]] against the *live* session is a three-step dance, because a running compositor is a single shared resource — see [[fleet-workflow]] for why only the keeper does this, never a parallel worker.

## Kill the running instance
- The instance has a 2-second exit-confirmation window (the same one behind `Super+Escape`): one `SIGTERM` only *arms* it, a second `SIGTERM` within ~2s actually quits.
- Find the pid, then double-tap: `ps -ef | grep "kalin-wm/build/kalin-wm -s" | grep -v grep`, then `kill -TERM <pid>; sleep 0.5; kill -TERM <pid>`, then `sleep 2; kill -0 <pid>` (that check should fail once it is really down).
- Kill by pid, not `pkill -f` — a `-f` pattern run from a Bash-tool script matches the script's own command line and kills the shell.

## Relaunch
- `export XDG_RUNTIME_DIR=/run/user/1000`, then `nohup /run/current-system/sw/bin/kalinwm > /tmp/kalinwm-restart.log 2>&1 & disown`.
- Always re-open a terminal attached to Kalin's persistent `claude` tmux session (his own Claude Code runs inside it), not a bare shell: `export WAYLAND_DISPLAY=wayland-0`, then `foot -e tmux a & disown`.

## QML-only changes skip all of this
- [[quickshell-shell|Quickshell]] reads `~/environment/quickshell` live, so a QML edit needs no compositor restart: kill the shell and relaunch `qs` (with `KALIN_IPC_SOCKET`/`QS_CONFIG_PATH`/`WAYLAND_DISPLAY` set) to pick it up.

## Session caveat
- A compositor launched this way (detached `nohup`, not Kalin's own login-session terminal) is not part of any systemd-logind session — `GetSessionByPID` fails for it.
- This is harmless for most things, but session-authenticated actions (e.g. `backlight_set()`'s `SetBrightness` D-Bus call) fail with "Session is not in foreground, refusing" regardless of the targeted session_path, because logind checks the *calling* process's own session credentials.
- To test something session-dependent, Kalin relaunches `kalinwm` from his own real terminal instead.
