# Foreign toplevel

kalin-wm implements `wlr-foreign-toplevel-management-v1`, the Wayland protocol
that lets an external program enumerate and control windows. It is implemented in
the `foreign_toplevel.c` runtime module.

Through this protocol the [[quickshell-shell]] gets the live window list and can
activate, close, and fullscreen windows, and capture window thumbnails for
[[overview-mode]] and window peek.

Foreign-toplevel carries the window list; the [[ipc-socket]] carries the
[[viewport]] camera state. The shell's `CompositorService` chooses the kalin-wm
backend (this protocol) when `$KALIN_IPC_SOCKET` is set, otherwise it falls back
to a niri backend.
