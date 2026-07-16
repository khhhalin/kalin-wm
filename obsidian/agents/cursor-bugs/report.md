# cursor-bugs — worker report

- Branch: `worktree-agent-a4c6522b0d5af72cc`
- Files touched: `code/src/dwl.c`, `obsidian/tasks/cursor-bugs.md` (Status/Branch
  lines only), this report zone. All within scope. No implementation notes
  edited — bug fixes only (bug 3 does change the cursor's startup position;
  flagged below).

## Bug 1 — cursor shape not restored after move/resize/pan

**Root cause.** For the whole duration of a grab, `setcursor()` and
`setcursorshape()` deliberately ignore client cursor requests
(`cursor_mode != CurNormal && != CurPressed`). On release, `buttonpress()`
set the xcursor to "default" and stopped. The comment in `setcursor()`
describes the intended mechanism — a leave followed by an enter makes the
client re-request its cursor — but nothing ever sent the leave: the seat's
pointer focus never changed during the grab, and `wlr_seat_pointer_notify_enter`
is a no-op when the surface is already focused, so the client believed its
shape was still set and "default" stayed pinned until the next surface
crossing.

**Fix.** In the release branch of `buttonpress()`: keep the "default" set as a
fallback (covers no-surface-under-cursor and clients that never set a cursor),
then after dropping the grab call `wlr_seat_pointer_notify_clear_focus(seat)`
followed by `motionnotify(0, NULL, 0, 0, 0, 0)`. The forced leave + re-enter
makes the surface now under the cursor re-request its preferred shape;
`setcursor()`/`setcursorshape()` accept it because `cursor_mode` is back to
`CurNormal`. `time == 0` in the internal `motionnotify()` call means no focus
stealing (`pointerfocus()` guards `focusclient()` on `time`).

**Live verify.** Hover a surface with a non-default cursor (text field —
I-beam, or a browser link — hand). Super+drag the window a little and release
*without* moving afterwards: the I-beam/hand must come back immediately, not
stay an arrow until the pointer crosses a surface edge. Repeat with resize and
a viewport pan grab. Also confirm drag-and-drop and normal clicking still
behave (the leave/enter runs only when a move/resize/pan grab ends).

## Bug 2 — cursor image at (0,0) after DPMS wake

**Root cause.** The wake modeset resets the backend's hardware cursor plane to
(0,0), but wlroots' cached position (`wlr_output_cursor.x/y`) is still the
correct one. The attempted `wlr_cursor_move(cursor, NULL, 0, 0)` funnels into
`wlr_output_cursor_move()`, which **early-returns when the target equals its
cached position** (verified against wlroots 0.20.0 `types/output/cursor.c`,
~line 442), so the "refresh" never reached the hardware — exactly why the old
comment said it didn't work.

**Fix.** In `updatemons()`, replace the no-op move with a bounce: save
`cursor->x/y`, `wlr_cursor_warp_closest(cursor, NULL, 0, 0)`, then warp back
to the saved position. The second warp is a real move, so it re-commits the
plane at the true position. Both warps happen synchronously inside one
handler; no frame renders in between and (like the old call) no motion events
are sent to clients. Residual corner case: if the cursor sits exactly at the
layout's closest point to (0,0), both warps no-op — but that coincides with
the position the plane was reset to, so the image is already correct there.

**Live verify.** With the cursor parked somewhere clearly not top-left, let
all monitors DPMS off (or force off/on via the output-power path), wake them,
and check the drawn cursor is where it was left — not at the top-left corner —
*before* touching the mouse. Also sanity-check monitor hotplug (the bounce now
runs on every `updatemons()`): plug/unplug an external display, cursor image
should stay put / clamp sanely.

## Bug 3 — hacky initial cursor placement at startup

**Root cause.** Same early-return as bug 2: `run()` warped the cursor to *its
own current position* (`wlr_cursor_warp_closest(cursor, NULL, cursor->x,
cursor->y)`), which `wlr_output_cursor_move()` discards, so nothing was ever
placed deliberately — the image simply appeared wherever the hardware had it
until the first physical motion.

**Fix.** A clean fix was evident, so it's fixed rather than documented: warp
to the centre of the output layout (`sgeom` midpoint; `warp_closest` clamps
into the nearest output if the midpoint falls in a dead gap between monitors),
*then* derive `selmon` from where the cursor landed (previously selmon was
computed from the stale backend position). Intentional behavior change worth
noting at review: the cursor now starts centred on the layout instead of
top-left. No vault note models startup cursor position, so no note edits were
needed.

**Live verify.** Restart the compositor (VM run is fine): the cursor should be
visible at the centre of the monitor immediately, default arrow image, and not
jump on first mouse motion. On multi-head, `selmon` should be the monitor
containing the layout centre.

## Build / test results

- Baseline (before changes): `nix develop -c make clean all` exit 0,
  `nix develop -c make test-unit` exit 0 (all checks passed).
- After changes: both exit 0 again (all checks passed).
- No unit test added: all three fixes are wlroots-interaction plumbing with no
  pure-logic seam reachable by the unit tests (which link no wlroots);
  behavior verification is the live checklist above.
- VM tests / live compositor: **not run** (keeper-only per task brief).

## Commits

One commit per bug on this branch, plus one for the task-note status flip and
this report; see `git log main..`.
