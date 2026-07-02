# kalin-wm

kalin-wm is a personal Wayland compositor: a window manager built around an
[[infinite-canvas]] navigated by a [[viewport]] camera instead of fixed
workspaces. This note is the project's goal note — the source of intent.

kalin-wm is a [[dwl-fork]] and runs on [[wlroots]]. It keeps the suckless
philosophy — [[compile-time-config]], minimal dependencies, hackable C — and
adds a viewport-driven navigation model inspired by [[niri]] and [[driftwm]].

## What it is

Windows live on an unbounded 2D plane addressed by [[world-coordinates]]. The
user moves a [[viewport]] (camera) across that plane with [[pan]], [[zoom]], and
[[follow-mode]] rather than switching workspaces. New windows are auto-placed by
the [[column-layout]], or detached as an [[anchored-window]] at a fixed world
position. [[directional-focus]] jumps between windows by direction using a cone
search. [[crop-mode]] clips a window to a sub-region.

## Why it exists

kalin-wm encodes one person's entire environment in a single hackable binary. It
is **personal, not generic**: hardcoded preferences in [[compile-time-config]]
are encouraged. It is **monolithic**: one codebase, [[wlroots]] plus standard C
libs, no plugin system. If it breaks, it crashes, and the display manager
restarts the session.

The companion [[quickshell-shell]] provides the bar, overview, and notifications;
the two talk over the [[ipc-socket]] and [[foreign-toplevel]] protocol. The
[[test-vm]] runs both together as a real session for development, and the
[[nixos-session]] wires kalin-wm into the host as a login option.

## Status

Version 0.8-dev. The MVP is complete; v1.0 is in progress. Decisions and progress
live in the [[ledger]]; open and planned work is in the [[roadmap]]. Deep research
backing the design lives under [[research/README|research/]].

See also: [[agent-workflow]] · [[build-system]] · [[project-structure]] ·
[[stability]] · [[keybindings]] · [[test-vm]] · [[nixos-session]]
