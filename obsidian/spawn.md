# Spawn

Spawn launches an external program from a [[keybindings|keybind]]. kalin-wm forks
and `execvp`s the command; the child inherits the compositor's environment,
including `$KALIN_IPC_SOCKET` (see [[ipc-socket]]) and `$WAYLAND_DISPLAY`.

Default spawns: `Super+T` → `foot` (terminal), `Super+P` → `fuzzel` (launcher),
`Super+O` → `qs ipc call …` ([[overview-mode]]).

Spawn failures are reported (pipe-based error reporting / logging). A historical
crash when spawning a terminal with several windows open was traced to `resize()`
touching an under-initialized client and fixed; see [[stability]].
