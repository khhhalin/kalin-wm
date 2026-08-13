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
  read as chaotic rather than useful. Plain click-to-focus is unaffected — only
  the two focus-*switching* binds are gated. (`swap-dir` was also cited here as
  position-not-focus; it was removed 2026-08-13 with the [[connection-graph]].)

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
- **Overlap** (`⇧O`, `toggle-overlap` / `ACT_TOGGLE_OVERLAP`) — toggle, still
  wired. **Dormant** as of 2026-08-13: its consumer `resolve_growth_overlap()`
  went with the [[connection-graph]] removal, so the flag toggles + broadcasts
  (`"focused":{"overlap":bool}`) but has no effect until the Phase 3 rail
  grow-push ([[layout-impl]]). Backed by the per-`Client` `allow_overlap` flag.
- **Swap** (`Ctrl+Arrows`, `swap-dir`) — **compositor action removed 2026-08-13**
  with the [[connection-graph]] (`swap-dir`/`ACT_SWAP_DIR` no longer exist). The
  Phase 2 rail 1D-swap re-adds a swap on the same chord. *(Shell hint removed too — see below.)*
- **Link** (`L`, `link-pick` / `ACT_LINK_PICK`) — **compositor action removed
  2026-08-13** with the [[connection-graph]]. The chord is reserved for the
  Phase 5 overlay-pin. *(The shell button is a cross-repo follow-up — see below.)*

> [!done] Cross-repo follow-up resolved (2026-08-13, kalin-shell `5788f49`)
> The **Swap** and **Link** hints were removed from `WindowActions.qml`, and
> `ConnectionLines.qml` / `LineGeometry.qml` (which read the removed
> `connections`/`pending_connect` state) were deleted, in `~/environment/kalin-shell`.
> The shell now loads clean against both the old and the new compositor.

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
