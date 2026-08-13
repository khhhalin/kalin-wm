# Float-under-cursor & attached overlay

The two non-rail placement buckets from [[layout-rethink]] — **float-under-cursor**
(menu spawns) and **attached overlay** (a child window pinned to and following a
host) — built in **layout Phase 4 + Phase 5 (DONE 2026-08-13)**. The [[rail]] is the
default keyboard-spawn bucket; these two are the exceptions to it. Both leave
`rail_prev == rail_next == NULL` (off-rail).

Code: `code/src/modules/dock/floatprep.c` (the float hint table),
`mapnotify()`/`unmapnotify()`/`resize()` in `code/src/dwl.c`, the `overlay-pin`
IPC in `code/src/modules/ipc.c`, `ACT_OVERLAY_PIN` in the bind DSL. Unit-tested in
`code/tests/test_float.c` and `code/tests/test_overlay.c`.

## Float-under-cursor (Phase 4)

**The signal.** There is no compositor-side way to tell a menu-spawn (right-click)
from a keyboard-spawn — both arrive via the same tmux spawn path. So the shell
**arms the intent ahead of the spawn**, exactly as it does for docking: a new IPC
command `float-next <appid>` (see [[ipc-socket]]) registers a one-shot hint in
`floatprep_pending[]` (`floatprep.c`, a verbatim twin of [[dwl-fork|dockprep]]).
`mapnotify()` consumes it (`floatprep_consume()`) right after the dockprep consume;
on a match the window takes the float branch instead of the rail branch. Default
(no hint) = rail.

**Placement — the non-obscuring offset rule.** The float branch marks the window
`isfloating` (so it's camera-bypassed and never inserted into the rail — its
`rail_prev`/`rail_next` stay NULL) and places its corner a small margin
(`FLOAT_CURSOR_MARGIN`, 24px, `kalin.h`) **clear of the cursor**, expanding toward
whichever side of the cursor has more room:

- x: if there's at least as much world room to the right of the cursor as to the
  left, the window's **left** edge starts at `cursor.x + margin`; otherwise its
  **right** edge ends at `cursor.x - margin` (`x = cursor.x - margin - width`).
- y: same test vertically against the monitor's world extent.

So the clicked point (cursor) is always left outside the window's rect — the window
never covers what was clicked. Room is tested in world coords against the monitor's
own visible extent (`SCREEN_TO_WORLD` off `cursor` + `mon->w` / zoom), so it's
correct under pan/zoom and on an offset second monitor. The pure geometry is
unit-tested (`test_float.c`, four cursor quadrants + a monitor-offset case).

**Runtime-verified (nested, 2026-08-13):** `float-next floatwin` then a `foot
--app-id=floatwin` mapped at `(640,20)` near cursor world `(1371,549)` — expanded
up-left (cursor was bottom-right of the output), off-rail. Proof it's off-rail: a
rail window spawned *after* the float spliced right of the previous **rail** member
(id 2 → id 4), skipping straight over the float (id 3) — the float was never in the
chain.

## Attached overlay (Phase 5)

TODO (filled in with the Phase 5 implementation).

See also: [[rail]] · [[layout-rethink]] · [[layout-impl]] · [[ipc-socket]] ·
[[floating-windows]] · [[crop-mode]] · [[directional-focus]] · [[keybindings]]
