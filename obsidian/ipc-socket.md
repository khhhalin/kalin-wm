# IPC socket

The IPC socket is a unix-domain socket that exposes the compositor's state to
external programs. Its path is exported in the `$KALIN_IPC_SOCKET` environment
variable, set before [[kalin-wm]] forks its startup command (so children inherit
it). It is implemented in the `ipc.c` runtime module.

The socket broadcasts [[viewport]]/compositor state as newline-delimited JSON
(camera x/y/zoom, [[follow-mode]] flags, [[crop-mode]] active, focused window).
It accepts camera commands from clients: `pan`, `zoom`, `zoom-reset`,
`follow-toggle`.

The [[quickshell-shell]] uses the socket (via its `KalinViewport` service) to
mirror the camera and drive it. The socket complements [[foreign-toplevel]],
which carries the window list; together they are the shell-integration channel.
