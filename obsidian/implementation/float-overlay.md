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

A **directed child→host pin + world-space offset**: a child window tracks a host
window at a fixed offset, following it wherever the host goes. The motivating case
is the "Discord-on-Minecraft" overlay — crop the Discord window, drop its opacity,
pin it in a corner of Minecraft; move Minecraft (rail-swap, drag, gap-close) and the
Discord overlay tracks it. Crop and per-window opacity **already** apply off the
child's own `c->geom`/`c->opacity`, so once the geom tracks the host the whole case
works with **no crop/opacity code changes**.

**Data** (`Client`, `kalin.h`): `Client *overlay_host` (NULL = not an overlay) plus
`int overlay_off_x, overlay_off_y`. Directed only (child→host, never the reverse); a
host may have several children; a child is never its own host, and hosts don't chain
(the pin refuses a host that is itself a child).

**The follow — one hook at the end of `resize()`** (`dwl.c`). Because *every* window
position flows through `resize(c, geom, interact)` (`arrange()` doesn't position),
one walk there covers **all** host-move sources — drag, rail-swap, gap-close,
grow-push, `setmon`, dock, persistence restore. After `resize()` writes `c->geom`, it
walks `clients` for any `child->overlay_host == c` and resizes each to
`{host.geom.x + off_x, host.geom.y + off_y, child.w, child.h}`. **Re-entrancy guard:**
a static `overlay_following` flag suppresses the walk while inside it — the child's own
`resize()` would otherwise re-enter; one level of suppression suffices because a child
is never its own host and hosts don't chain.

**Entry points.** Direct (scriptable): the `overlay-pin <child-id> <host-id> [dx dy]`
IPC command (see [[ipc-socket]]) → `overlay_pin()` in `dwl.c`; omitting dx/dy captures
the current child−host offset. Interactive: `ACT_OVERLAY_PIN` on the freed **`Super+L`**
arms the focused window as a child (`overlay_pin_arm()` sets `pending_overlay_child`),
and the **next-clicked** window becomes its host (consumed in `buttonpress()` at the
current offset). `overlay_pin()` takes the child **off the rail** (`rail_remove()`,
closing the gap and nulling its rail pointers), marks it `isfloating` (camera-bypassed
like the rest of the free/float bucket), and snaps it to the host immediately.

**Exclusions.** An overlay child is decoration, not an independent window: it's
excluded from the rail (never inserted — `overlay_pin()` removes it if it was on it)
and skipped in `directional-focus` targeting (`cone_search_focus()`,
`directional_focus.c`, guards `c->overlay_host`, like a panel). On **host close**
(`unmapnotify`), every child's `overlay_host` is nulled so they become free floats
rather than tracking a freed pointer — the same dangling-ref discipline the rail uses;
a closing child needs nothing (its own ref dies with it). A pending interactive pin is
also dropped if its armed child closes.

**Config-coverage caveat (deploy blocker, not shipped).** Adding `ACT_OVERLAY_PIN`
makes `overlay-pin` a known action, and the bind engine's coverage check
(`bind_check_coverage()`) `die()`s at startup if the on-disk `binds.conf` doesn't
bind *or* `unbind` every known action. `default_binds.h` binds `Super+l -> overlay-pin`,
but the user's live `~/.config/kalin-wm/binds.conf` does **not** yet — so **this branch
must not be deployed to the real session until that file gains a `bind Super+l ->
overlay-pin` (or `unbind overlay-pin`) line.** The nested smoke sidesteps this with a
throwaway `XDG_CONFIG_HOME` (default binds cover it). This is a genuine follow-up, left
undone here because editing the user's live config wasn't authorized.

**Runtime-verified (nested headless, 2026-08-13):** pinned child id 3 → host id 1 at
offset (20,20); the pin snapped the child to host-origin + offset. Moving the host
(via `dock` → `resize()`, since headless doesn't render frames so the rail-swap spring
never advances) to origin `(900,500)` made the follow hook fire — child ended at
`(920,520)` = host `(900,500)` + `(20,20)`. The child tracked the host at the fixed
offset.

See also: [[rail]] · [[layout-rethink]] · [[layout-impl]] · [[ipc-socket]] ·
[[floating-windows]] · [[crop-mode]] · [[directional-focus]] · [[keybindings]]
