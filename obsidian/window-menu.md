# Window menu (hold-Super action menu)

**Status: implemented, revamped 2026-07-09.** Bound to `hold Super`
(`ACT_WINDOW_MENU`, `code/src/modules/binds/bind_actions.c`); shell surface is
`quickshell/modules/WindowActions.qml`. This note describes the current
shipped design — check `WindowActions.qml` directly if it drifts again (see
the [[ledger]] for the history of this note being stale before).

- It is a **key-hint overlay, not a clickable menu**: while Super is held and
  a window is focused, it shows that window's available actions with their
  key hints; the user presses the key (Super still held) to invoke the real
  compositor keybind. The overlay's input mask is intentionally empty
  (click-through) — Super doubles as the window-drag modifier, so a real
  clickable region big enough to cover the buttons would swallow drags.
- Driven entirely by the [[ipc-socket]]'s `"focused"` object
  (`KalinViewport.qml`) — no dedicated menu protocol.
- **Focus is locked to whichever window the menu opened on (2026-07-09).**
  `focusstack()` (`Super+J`/`K`) and `focus_directional()` (`Super+Arrow`)
  both early-return while `menu_shown` is true (`code/src/dwl.c`) — switching
  focus out from under an open menu used to reposition it (new anchor
  window) and re-pan the camera ([[follow-mode]]) at the same time, which
  read as chaotic rather than useful. `swap-dir` (`Super+Ctrl+Arrow`, moves a
  window's *position*, not focus) and plain click-to-focus are unaffected —
  only the two focus-*switching* binds are gated.

## Layout: arc vs. dock

- **Normal case**: an Android-style arc of round buttons flows out of the
  focused window's right edge, curved (parabolic bow) toward the screen.
- **A window spanning (≥85% of) the screen's width has no room to its right**
  for that arc — it would fly off-screen. Past that width threshold
  (`radial.dockMode` in `WindowActions.qml`) the menu switches to a straight
  vertical dock pinned to the screen's right edge instead, flying in from
  off-screen rather than bowing out from the window edge. Same buttons, same
  on/off states, different anchor only.
- **A window that isn't full-width but still sits close enough to the right
  screen edge that the arc would run past it** (2026-07-09,
  `viewport_menu_reveal()`, `code/src/modules/viewport/viewport_ops.c`,
  called from the `ACT_WINDOW_MENU` bind case in `dwl.c`): the camera pans
  right by just enough screen-space to clear an ~300px reserve past the
  window's right edge, animated like any other camera pan. Skipped entirely
  above the same 85%-width dock threshold the shell uses, since a docked
  menu doesn't care where the window sits.

## What it exposes

Each entry mirrors a real keybind (see [[keybindings]]); a toggle action
additionally shows on/off via a small indicator dot plus a filled/bright vs.
neutral button treatment when on:

- **Close** (`Q`) — momentary, no state.
- **Fullscreen** (`E`) — toggle, state = `KalinViewport.focusedFullscreen`.
- **Crop** (`C`) — toggle, state = `KalinViewport.cropActive`.
- **Overlap** (`⇧O`, `toggle-overlap` / `ACT_TOGGLE_OVERLAP`) — toggle. When
  on, the focused window is exempt from `resolve_growth_overlap()`'s
  connection-graph push-out (see [[connection-graph]]): it can grow or be
  positioned over another window instead of shoving it aside. Backed by a
  new per-`Client` `allow_overlap` flag; state mirrored over IPC as
  `"focused":{"overlap":bool}`.
- **Swap** (`Ctrl+Arrows`, `swap-dir`) — momentary directional action, no
  state; renamed from the old "Move" label, which described a `move-column`
  action that no longer exists (see [[column-layout]]).
- **Link** (`L`, `link-pick` / `ACT_LINK_PICK`, added 2026-07-10) — toggle,
  state = `KalinViewport.pendingConnect`. Arms the focused window as a
  pending [[connection-graph]] source; a live rubber-band line (same dotted/
  sparkle rendering as a real connection) follows the cursor from that
  window until the next click on a different window completes the link, or
  Super is released / empty canvas is clicked, which cancels it. See
  [[connection-graph]]'s "Manual create/sever" section for the full
  mechanism and why it's key-armed rather than a literal QML drag handle.

**Removed 2026-07-09**: the old "Tile/Float" toggle. It read
`KalinViewport.focusedFloating`, a field the compositor stopped sending once
[[column-layout]]/[[anchored-window]] were removed — so it had been silently
dead (always showing "Float", doing nothing useful) since that removal,
undetected until this pass.

## Why

- A single discoverable per-window menu beats memorizing one-off chords,
  especially for less-frequent actions (crop, overlap) — same rationale as
  the original design.

## Related

[[connection-graph]], [[crop-mode]], [[keybindings]], [[quickshell-shell]],
[[roadmap]].
