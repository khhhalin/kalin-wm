# Compile-time config

- Most of kalin-wm's configuration is still compile-time, in the dwm
  tradition — colors, spawn commands, and tunables like `focusringpx` (see
  [[focus-ring]]) and `offscreen_indicator_*` (see [[off-screen-indicators]])
  live in `code/config/config.h` (copied from `config.def.h`); the user
  edits it and rebuilds.
- **[[keybindings]] are the one exception, and are no longer compile-time.**
  They moved to a runtime DSL (`bind <chord> -> <action> [args]`) parsed
  from `code/config/default_binds.h` (shipped defaults) with an optional
  user override at `~/.config/kalin-wm/binds.conf` (written only if
  missing) — see [[keybindings]] for the full current table and syntax.
  There is still no runtime IPC for *settings* beyond binds and the
  existing camera/[[connection-graph]] commands (see [[ipc-socket]]).

- Compile-time-only config (for everything but keybinds) is a deliberate
  non-goal-driven choice: it follows the [[dwl-fork|suckless philosophy]]
  and keeps the binary a personal artifact (see [[kalin-wm]]).
- New user-facing tunables must get a default in `config.def.h`.
