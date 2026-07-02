# Compile-time config

All of kalin-wm's configuration is compile-time, in the dwm tradition. The user
edits `code/config/config.h` (copied from `config.def.h`) and rebuilds; there is
no runtime config file or runtime IPC for settings.

The config holds the [[keybindings]] table, spawn commands, colors, and tunables
like `focusringpx` (see [[focus-ring]]) and `offscreen_indicator_*` (see
[[off-screen-indicators]]).

Compile-time-only config is a deliberate non-goal-driven choice: it follows the
[[dwl-fork|suckless philosophy]] and keeps the binary a personal artifact (see
[[kalin-wm]]). New user-facing tunables must get a default in `config.def.h`.
