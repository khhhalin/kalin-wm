# Roadmap

- Planned and open work for [[kalin-wm]].
- Completed work and decisions go in the [[ledger]]; this note is the forward-looking backlog.
- It supersedes the old root `ROADMAP.md`.

## Direction (2026-07-01)

- The goal is to **daily-drive** kalin-wm on the [[infinite-canvas]]. The
  original plan here named the (since-removed) [[column-layout]] as the
  primary motion; the actual current model is full free positioning + a
  [[connection-graph]] between spawn-adjacent windows (2D from the start,
  not column-scrolling-first) — see [[connection-graph]] and the [[ledger]]
  for when/why that changed.
- Current priorities, in order:
1. **[[stability]] / crash-proofing** — the main blocker to daily use.
2. **[[quickshell-shell]] integration** — taskbar, [[overview-mode]], notifications.
3. **Keep cutting dwl heritage** — continue simplifying inherited code (XWayland, tiling params, and tags already removed — see [[ledger]]).
   - Modularization (splitting `dwl.c` into `code/src/modules/*` TUs) is
     ongoing, not finished: 5169 -> 4171 lines in one 2026-07-09 sweep
     (connection-graph, directional-focus, client-anim, PTY, window-size-
     history all extracted — see [[ledger]] and [[connection-graph]]), but
     dwl.c had also grown back up from a prior 3884-line low as fast as
     features landed. Next candidates once more accumulates: `main()`/
     `setup()`/the wlroots-object-lifecycle listener handlers are the
     intentional irreducible core and shouldn't be targeted.

- [[zoom]] is **parked** — dropped from active focus and being rethought.
- Target: laptop screen first, occasional external monitor second.

## Blocking v1.0

- [[stability]] — Phase 0 audit (all 23 items fixed and re-verified). ✅ done.
- Ongoing crash-proofing continues as the top daily-drive priority.

## In progress

- **[[multi-camera]] — independent per-monitor viewports** (started 2026-07-15):
  single-view shared world, camera ops follow the cursor's monitor. **Phase 1
  (core) has landed** — `Monitor.cam` replaces the global viewport, every
  transform routes through the client's holder (`c->mon`) or the cursor monitor,
  verified with independent dual-output panning (25 unit tests green, no
  single-monitor regression). Remaining: **Phase 2** drag hand-off +
  send-to-monitor bind + cross-camera edge severing; **Phase 3** IPC/shell
  per-output keying of the `viewports` array; **Phase 4** per-monitor wallpaper,
  off-screen indicators, overview polish. Full breakdown + touch list in the note.

- Verify the [[nixos-session]] end-to-end after `nixos-rebuild switch`: quickshell bar auto-starts, `Super+T`/`Super+P`/`Super+O` work, and the taskbar lists running apps.
- **[[protocols]] — implement missing popular Wayland protocols**, starting
  with `xdg-toplevel-icon-v1` (confirmed missing: our own log warns
  `compositor does not implement the xdg-toplevel-icon protocol` every
  session). See [[protocols]] for the full missing/implemented breakdown and
  priority order.
  - **Rule for this work (and any future protocol addition): write it as a
    new module under `code/src/modules/protocols/`, not into `dwl.c`.**
    `dwl.c` keeps only the one-line `wlr_*_create()` registration call in
    `setup()`; the listener setup and logic go in the module. This continues
    the modularization direction already tracked in the [[ledger]]
    ("modularization step 1/2") and in [[dwl-fork]] — the goal is to shrink
    the monolith, not grow it every time we add a protocol.

## Planned — [[shaders]] (designed 2026-07-15; Phase 0 infra + paper-mode core in tree, gated off)

- GPU fragment shaders at two levels: **per-window** (shadow, rounded corners,
  blur-behind, dim/focus, paper-mode reading tint) and **camera/output**
  (color-grade/CRT/vignette over the whole [[viewport]]). Full design, phasing,
  and risks in [[shaders]].
- **Paper mode (per-window reading tint) — fully wired, GPU-unverified:** both
  fleet tasks merged 2026-07-15 (`paper-shader-core` machinery +
  `paper-window-bind` driver: `Super+i` toggle, `Rule.paper` appid column,
  per-frame overlay from `rendermon()`) — as-built in [[shaders]]. Remaining:
  the live GPU gate — `WLR_RENDERER=gles2` + `KALIN_SHADER_DIR` set (shaders_dir
  is CWD-relative), verify passthrough is identical and paper renders upright
  (shared vertical-flip assumption).
- Shape is forced by the [[scene-graph]]: `wlr_scene` exposes no GLSL hook, so the
  plan keeps the scene and drops to **raw GLES2 only at the compositing stage**
  (offscreen texture → shader pass → output). Requires pinning `WLR_RENDERER=gles2`
  and disabling shaders gracefully otherwise.
- **Subsumes** the "window shadows" and "rounded corners" items below — both are
  the first per-window shader passes. Effects land incrementally on one shared
  offscreen-render infrastructure.

## Recently completed

Pointers only — chronology is in git, detail is in each subsystem's
implementation note. Trimmed from full narrative 2026-07-15.

- [[bar-tuis]] — Textual TUIs for all seven docked bar panels; battery out of the
  QML SidePanel; DockedPanel lifecycle races + `ipc.c` line-framing fixed.
  (Host `nixos-rebuild` that puts `kalin-bar-tui` on PATH — **status unverified** as of 2026-07-15.)
- [[quickshell-shell]] ported to the kalin-wm backend (`CompositorService`/[[foreign-toplevel]]).
- [[nixos-session]] starts shell + terminal via the `kalin-wm-session` wrapper.
- [[connection-graph]] replaced [[column-layout]]/[[anchored-window]] (free positioning + spawn-adjacency graph).
- [[window-menu]] (hold Super, `WindowActions.qml`).
- [[connection-graph]] forgiving drag-to-cut severing + menu-armed manual connect (`Super+L`).
- Trackpad [[gestures]] — 3-finger swipe pan (momentum coast) + pinch zoom.
- [[persistence]] rework — multi-instance identity keying, graph save/restore, `mkdir -p` fix.
- Resize grabs the nearest corner; `Super+Ctrl+BTN_LEFT` solo move — see [[connection-graph]].

## v1.0 features — open

- Window shadows — now the first per-window pass of [[shaders]] (behind-quad).
- **Session resurrection** (requested 2026-07-16): [[persistence]] restores
  geometry/connections but not the *processes* — after a restart the user
  reopens apps by hand. Wanted: respawn saved clients at startup.
  Complications scoped so far: the client PID's `/proc` cmdline is the
  foot *server* for every terminal window (`foot --server` — respawning it
  recreates zero windows; terminals need a tmux-session-aware path via
  `kalin-term`), and bar-panel clients (`kalin-*-panel-*`) must be skipped
  because DockedPanel respawns them itself. Also: saved `instance` indices
  survive only if respawn order matches last spawn order.

## Post-v1.0 — nice to have

- Rounded corners — a composite-time per-window pass of [[shaders]].
- Minimap (corner overview of all windows + viewport rectangle).
- Bookmarks (named [[viewport]] positions to jump to).
- Magnetic snapping (windows snap to each other / to a grid).
- Anchor-mode visual distinction (different border for an [[anchored-window]]).
- [[crop-mode]] on-screen banner; cursor-state feedback during pan/move/resize.

## Under consideration

- **Auto-pan when dragging a window past the viewport edge** (driftwm-inspired) —
  would extend `motionnotify()`'s `CurMove` branch. Investigated, not yet
  implemented. Its sibling half (gesture pan + momentum coast + pinch zoom, and
  spring-glide for group-drag / `swap_neighbor_dir()`) already shipped — see
  [[gestures]] and [[connection-graph]]. Edge-drag auto-pan is the one piece left.

## Known minor bugs (found 2026-07-09, not yet fixed)

- Cursor icon isn't reset to the pointer focus's own preferred shape after a
  move/resize/pan interaction ends — forced back to "default" instead
  (`code/src/dwl.c`, upstream dwl-heritage `TODO`).
- Cursor image jumps to (0,0) after all monitors wake from DPMS/sleep
  (`code/src/dwl.c`, `FIXME` in the output-config-apply path) — the fix
  attempted (`wlr_cursor_move(cursor, NULL, 0, 0)`) doesn't actually restore
  the prior position, per its own comment.
- Cursor's very first on-screen position at compositor startup is a hacky
  warp-to-last-position rather than a clean initial placement — minor,
  cosmetic, low priority.

## Already shipped (see [[ledger]])

[[infinite-canvas]], [[viewport]] [[pan]]/[[zoom]]/[[follow-mode]]/[[fit-all]],
[[buffer-scaling]], [[connection-graph]] (superseding [[column-layout]] +
[[anchored-window]]), [[directional-focus]], [[crop-mode]], [[focus-ring]],
[[off-screen-indicators]], [[persistence]], [[ipc-socket]],
[[foreign-toplevel]], multi-monitor, [[overview-mode]], [[window-menu]].
