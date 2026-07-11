# kalin-wm

- kalin-wm is a personal Wayland compositor: a window manager built around an [[infinite-canvas]] navigated by a [[viewport]] camera instead of fixed workspaces.
- This note is the project's goal note — the source of intent.

- kalin-wm is a [[dwl-fork]] and runs on [[wlroots]].
- It keeps the suckless philosophy — [[compile-time-config]], minimal dependencies, hackable C.
- It adds a viewport-driven navigation model inspired by [[niri]] and [[driftwm]].

## What it is

- Windows live on an unbounded 2D plane addressed by [[world-coordinates]].
  Every window is free-positioned at a persistent world position — there is
  no tiled/floating dichotomy and no auto-arranging layout mode any more
  (the old [[column-layout]] and [[anchored-window]] split was removed
  entirely; see [[connection-graph]]).
- The user moves a [[viewport]] (camera) across that plane with [[pan]], [[zoom]], and [[follow-mode]] rather than switching workspaces.
- New windows spawn to the right of whichever window was focused when they
  were created, and stay linked to it in a [[connection-graph]] — up to 8
  neighbor connections per window, one per compass direction. The graph
  drives group-drag, directional swap (`Super+Ctrl+Arrow`), splicing a gap
  closed when a window in the middle of a line closes, and pushing
  neighbors out of the way when a window grows. [[persistence]] saves and
  restores both position/size and the graph itself across restarts.
- [[directional-focus]] jumps between windows by direction using a cone search (unrelated to the connection graph — pure geometry).
- [[crop-mode]] clips a window to a sub-region.
- [[screenshot-ui]] is a niri-style interactive screenshot tool (`Super+Shift+S`): drag-select a region or capture the whole monitor, save to disk and/or clipboard.

## Why it exists

- kalin-wm encodes one person's entire environment in a single hackable binary.
- It is **personal, not generic**: hardcoded preferences in [[compile-time-config]] are encouraged.
- It is **monolithic**: one codebase, [[wlroots]] plus standard C libs, no plugin system.
- If it breaks, it crashes, and the display manager restarts the session.

- The companion [[quickshell-shell]] provides the bar, overview, and notifications; the two talk over the [[ipc-socket]] and [[foreign-toplevel]] protocol.
- The [[test-vm]] runs both together as a real session for development.
- The [[nixos-session]] wires kalin-wm into the host as a login option.

## Status

- Version 0.8-dev.
- The MVP is complete; v1.0 is in progress.
- Decisions and progress live in the [[ledger]]; open and planned work is in the [[roadmap]].
- Deep research backing the design lives under [[research/README|research/]].

See also: [[agent-workflow]] · [[build-system]] · [[project-structure]] ·
[[stability]] · [[keybindings]] · [[test-vm]] · [[nixos-session]]
