# Ledger

- **Frozen archive (as of 2026-07-15).** This is no longer the running record — the vault graph of object notes is (see [[agent-workflow]]). Kept for its historical decision trail; not appended to going forward.
- Historical running log of decisions, progress, and changes for [[kalin-wm]]. Newest first.
- Dates are absolute.

## 2026-07-15 — [[multi-camera]] phase 1: per-monitor independent cameras (core)

Replaced the single global `viewport` camera with a per-`Monitor` `cam` over the
one shared world (single-view: every window keeps one global position, renders
on one monitor, and the monitor under the cursor owns all camera input). This is
the "park one monitor, roam the other" feature Kalin asked for.

- Transform macros `WORLD_TO_SCREEN_*`/`SCREEN_TO_WORLD_*` now take a monitor and
  fold in its layout offset (`m->m`); for a single monitor at (0,0) the math is
  byte-identical to before. **Macro param is `mon_`, not `m`** — a param named
  `m` gets textually substituted into the `->m` box-member access
  (`(m)->m.x` → garbage), which is exactly the first compile error hit.
- Swept every camera reader onto the right camera (holder `c->mon` for client
  transforms, `selmon` for bind-level pan/zoom/fit/follow/overview/gestures):
  `viewport_ops.c` (ops + `viewport_tick` iterates all animating cams),
  `overview.c`, `gestures.c`, `crop_mode.c`, `connection_graph.c`,
  `client_anim.c`, `directional_focus.c`, `dwl.c`, `ipc.c`. Interactive-drag
  clamp now bounds to the window's own monitor (`m->m`), not the `sgeom` union.
- IPC: rects (`focused`/`connections`/`pending`) transform per-holder; added a
  `"viewports":[{output,x,y,zoom,...}]` array; kept the scalar `"viewport"` =
  selmon's camera so the shell OSD/KalinViewport keep working unchanged.
- Verified: build + 25 unit tests green; single-monitor nested no-regression;
  and — via the new headless harness (below) — **camera isolation proven
  numerically**: warp to HEADLESS-1 + `pan 300 0` moved only H-1's camera
  (0→300), warp to HEADLESS-2 + `zoom 1.5` moved only H-2's zoom (1.0→1.5),
  each output's other axes untouched; per-output `grim` shows independent
  scenes.
- **New: `tools/headless-mc/` — headless multi-output test harness.** kalin-wm
  on wlroots' headless backend (`WLR_HEADLESS_OUTPUTS=N` + pixman), driven over
  IPC, per-output `grim` screenshots, no GPU/session/real-desktop. Fills the
  gap the single-GPU test VM can't. Added two IPC commands it needs (also
  generally useful): `warp <x> <y>` (deterministic pointer → picks `selmon`,
  since headless has no real pointer) and `screenshot`/`screenshot-ui`. Wired
  into the kalin-wm skill + `tools/headless-mc/README.md`. Bug found and fixed
  building it: `warp`'s first cut called `motionnotify(0,…)`, whose selmon
  update is gated on non-zero event time (time==0 = internal focus-restore
  only), so selmon never moved — pass time=1 (deltas still zero).
- **Not yet running as the real session** (needs a compositor restart — Kalin's
  call). Wallpaper still tracks only selmon's camera (one shared scene tree;
  per-monitor wallpaper is phase 4). Hand-off (drag/keybind to move a window to
  the other monitor) and the shell per-output overlay work are phases 2–3.

## 2026-07-15 — [[screenshot-ui]]: freeze-frame, TUI-styled readout, WYSIWYG export

Three user-visible upgrades to the Super+Shift+S screenshot UI, all in
`screenshot_ui.c` (+ a small `capture.c` split):

- **Freeze-frame**: the monitor's scene is rendered once at open
  (`capture_render_native`, now public) and shown as a scene buffer under
  the dim — the world visibly stops while selecting, like niri.
- **TUI-styled readout**: a bottom-center panel (warm-amber palette matching
  [[bar-tuis]], opaque `#1e1915` bg + amber border) shows the live selection
  `W X H   AT (X,Y)` plus the key hints. Text is rasterized with a
  hand-rolled 5x7 bitmap font (digits/uppercase/symbols) into a custom
  owned-pixels `wlr_buffer` — no font library, ~90 lines. (An external
  public-domain font8x8 header was considered and rejected: no new deps,
  and no unauditable vendored tables.)
- **WYSIWYG export**: confirm now crops from the frozen pixels
  (`capture_export_pixels`, split out of `capture_export_selection`) instead
  of re-rendering the scene. This also fixes a real pre-existing bug: the
  re-render happened *before* the overlay was destroyed, so every saved
  screenshot included the 35% dim (and border pixels at the selection edge).

Plumbing: `ipc.c` gained `screenshot-ui` and `screenshot` commands (open the
UI / immediate capture — same as the binds), added for host-side testing of
a nested instance (input can't be injected into a nested compositor, IPC
can) and kept as a real API for future shell widgets. The owned-pixels
buffer impl (`pixel_buffer_*`) frees its data only when the renderer drops
its last lock, so texture-upload lifetime can never see freed memory.

Verified: nested-on-host instance (`WLR_BACKENDS=wayland ./build/kalin-wm -d`),
UI opened + self-captured over the new IPC commands — freeze, dim, amber
border, and the info panel all render; font legible. Build + 25 unit tests
green. Not yet exercised: a live drag (info text re-rasterizes on change —
same code path) and the confirm keybind end-to-end; both worth a quick real
run. Dev gotcha discovered on the way: plain `make` only prints usage — the
build target is `make all`; a stale `build/kalin-wm` cost a debugging loop.

## 2026-07-15 — [[bar-tuis]]: custom Textual TUIs for all seven docked bar panels + IPC framing fix

Every docked panel of the bar now runs a custom TUI from one new suite
(`tools/bar-tuis/kalin_tuis/`, dispatcher `kalin-bar-tui <panel>`), replacing
btop/nmtui/bluetuith/wiremix/the fzf clip-picker loop, and pulling battery out
of the QML SidePanel into its own docked panel (`SystemPanel.qml` deleted;
SidePanel is calendar-only now). Shared warm-amber theme matched to foot.ini +
Theme.qml — full design/backend map in [[bar-tuis]]. Packaging: one `barTuis`
wrapper in home-config/desktop.nix (textual+psutil+dbus-fast env, backend CLIs
PATH-prefixed) + parallel `testBarTuis` in test-vm/vm.nix; bluetuith/wiremix/
clipPickerLoop dropped from home-config (btop kept as a general tool,
`kalin-clip-picker` kept for Super+V). **Rebuild not yet run** — the QML
already invokes `kalin-bar-tui`, so panels are broken-by-design until
`nixos-rebuild switch` lands the wrapper (dev shim on PATH covers testing).

Real bugs found and fixed along the way:

- **`DockedPanel.spawned` never reset on process exit** — a quit/crashed TUI
  (`q` is bound in every panel) left a permanently dead bar button. Fixed with
  `Process.onExited`.
- **Close-before-map race**: a panel closed while its first spawn was still
  starting no-op'd its undock/minimize (app_id not mapped yet), and the client
  then mapped *visible* via the armed dockPrep rect. Reproduced in the VM
  (cold python spawn there takes 60s+; host is sub-second). Fixed by guarding
  `firstSpawnDelay` on `open` + a 60-tick `lateSpawnSettle` re-assert.
- **`ipc.c` broadcast desync** (pre-existing, exposed by panel churn): short
  writes on the non-blocking client fds truncated a state line mid-record and
  every later broadcast glued onto it — quickshell logged `bad state line`
  storms and panels stuck open on stale `dock_hover`. `ipc_client_send()` now
  keeps line framing (per-client resync `\n`), and `ipc_build_state()`
  restores the trailing `\n` on truncation. One truncated record is still
  lost (self-healing); full per-client output buffering remains a possible
  follow-up. Build + 25 unit tests green.
- **Mixer volume source**: `pw-dump`'s node `Props` disagreed with reality for
  ALSA devices (WirePlumber keeps volume/mute on the device *route*) — the
  panel showed 100%/unmuted while the sink was 125%/muted. Switched to
  `wpctl get-volume <id>` per node (already cubic-scaled, no cbrt).

Two more found live the same day, once DP-3 was plugged in:

- **Outputs JSON corrupted by snprintf truncation** (`ipc_build_state`): the
  modes/outputs/conns builder loops broke out when an entry didn't fit —
  but snprintf had already written the partial entry into the buffer, and
  the final `%s` (which reads to the NUL, not to the tracked length) emitted
  it: `{"width":1280,"height":]`. With DP-3's long mode list this made
  *every* state line unparseable (the real source of the "bad state line"
  storm — the short-write framing issue above was only a secondary path).
  Fixed by restoring the NUL at each break site; buffers bumped
  (IPC_BUF_SIZE 8192→16384, outputs 2048→4096, modesbuf 1024→2048) so two
  monitors' full mode lists actually fit. Build + 25 unit tests green.
- **Per-monitor bars never actually worked: `screen` property shadowing.**
  `BottomBar.qml` and `SidePanel.qml` (roots are `PanelWindow`) declared
  `required property ShellScreen screen`, which *shadows* PanelWindow's own
  `screen` property — the caller's `screen:` binding set the shadow and the
  real window screen stayed default, so every monitor's bar stacked on the
  first output (two bars on LVDS-1, none on DP-3). Removed the declarations;
  the built-in property now receives the binding and each monitor gets its
  own bar. The 2026-07-11 "bar on every screen" work created the per-screen
  *instances* but their windows all landed on one output.

Verified: all 7 panels smoke-tested headless (Textual pilot) against live
host backends; mixer/stats/wifi/battery visually verified docked on the host
(via IPC dockprep + grim); VM pass with `--override-input quickshell-config
path:…` (the pinned input is the GitHub repo, not the working tree!) —
hover→spawn→dock, close-on-leave, coordinator swap, and graceful
degradation (BlueZ "unit failed" card, 0 wifi networks) all confirmed;
close-before-map race re-tested clean after the fixes.

Two requests working toward a first stable release:

- **Resize grabs the nearest corner, not always bottom-right.** `moveresize()`
  (`dwl.c`, `CurResize` case) used to unconditionally warp the cursor to
  `grabc`'s bottom-right corner and `motionnotify()` always anchored
  `geom.x/geom.y` (top-left) as fixed. Now `moveresize()` picks whichever of
  the 4 corners is nearest the cursor at grab time (comparing against the
  window's center), stores the *opposite* corner as a fixed world-space
  anchor (`resize_anchor_x/y`, new file-statics mirroring `grabcx/grabcy`),
  warps the cursor to the grabbed corner, and sets a matching
  nw/ne/sw/se-resize cursor icon. `motionnotify()`'s `CurResize` branch is now
  anchor-relative (`x = MIN(cursor, anchor)`, `width = abs(cursor - anchor)`),
  which reduces to the old bottom-right-anchored behavior as a special case
  — no regression for that corner, and the other 3 now work the same way.
- **`Super+Ctrl+BTN_LEFT` on a window: solo move.** This chord already existed
  for direct-manipulation camera pan (`ACT_VIEWPORT_PAN_GRAB`), but only did
  anything on empty canvas — clicking it on a window was a silent no-op.
  Added a new `CurMoveSolo` cursor mode: same grab-offset math as normal
  `Super+BTN_LEFT` move, but `motionnotify()`'s connected-component glide
  block (`collect_component()` + drag-along) only runs for plain `CurMove`,
  not `CurMoveSolo` — so the window moves alone, the rest of its
  [[connection-graph]] component stays put, and nothing is severed (this
  path never calls `sever_connection()`). `ACT_VIEWPORT_PAN_GRAB` now tries
  `moveresize(CurMoveSolo)` first and only falls back to
  `viewport_pan_grab_start()` if there's no normal (managed, non-fullscreen)
  client under the cursor — preserves the original pan-on-empty-canvas
  behavior exactly.

Verified: clean rebuild + 25/25 unit tests. VM: right-click-dragged a
terminal's near-top-left area with Super held — cursor warped to that exact
corner and the window resized from there while the opposite (bottom-right)
corner stayed pinned. Separately, spawned a second terminal (connected via
spawn-adjacency to the first) and `Super+Ctrl`-dragged it across the
screen — it moved on its own while its sibling stayed exactly in place;
a plain `Super`-drag on the same window immediately after was unaffected
(still just the grabbed-window offset math, no change in the plain-`CurMove`
path). Didn't re-confirm the dotted connection-line rendering itself in this
pass — out of scope for this change, unrelated code path.

## 2026-07-12 — Removed the compositor's own overlay clock

The compositor drew its own bottom-right HH:MM digital clock
(`modules/ui/overlay_clock.c`, a hand-rolled seven-segment display built
from `wlr_scene_rect`s) alongside the [[quickshell-shell]]'s own bar clock
(`ClockButton.qml`) — a redundant second clock. Removed entirely at the
user's request, keeping only the shell's: deleted `overlay_clock.c`, its
`#include`/`overlay_clock_init()`/`overlay_clock_configure()` call sites in
`dwl.c`, and its tuning constants from `config.def.h` and the local
`config.h`. Updated `code/src/modules/README.md`'s module list and
[[wallpaper]] (which mentioned it drew into the same UI layer).

Verified: clean rebuild, 25/25 unit tests, live-restarted the real
compositor, screenshotted the desktop — only the shell's bar clock remains
visible, no separate floating clock element.

## 2026-07-12 — Move windows in overview mode; connection lines visible there too, inset toward center; fit-width/height center the camera on their axis

Three related requests, all touching [[overview-mode]] and [[connection-graph]]:

- **Move windows while overview is open.** `buttonpress()` (`dwl.c`) used to
  call `overview_select(c)` — jump the camera to the clicked window and
  close overview — on *any* click over a client while overview was active,
  including a Super-held click meant to start a move/resize grab
  (`bind_dispatch_button()` runs right after, further down the same
  function). That meant Super+drag could never actually drag anything in
  overview: the camera yanked away and overview closed the instant the
  button went down, before a grab had any chance to start — defeating the
  actual point of opening overview (rearrange windows while seeing all of
  them). Fixed with one guard: `if (overview_is_active() && !super_held)`.
  A plain click (no Super) still jumps/closes, unchanged. Move/resize
  itself needed no changes — `motionnotify()`'s `CurMove` handling already
  goes through `SCREEN_TO_WORLD_X/Y` (zoom-aware), so it was already correct
  at whatever zoom overview happens to be at.
- **Connection lines visible in overview, not just while Super is held.**
  `ConnectionLines.qml` ([[quickshell-shell]]) gated its own visibility on
  `KalinViewport.superHeld` alone. Added a new `"overview":true|false` field
  to the IPC state broadcast (mirrors `overview_is_active()`), a matching
  `KalinViewport.overviewActive` property, and changed the visibility
  condition to `superHeld || overviewActive` — the graph is most useful to
  see precisely when looking at the whole desktop at once, which is exactly
  when Super usually isn't being held.
- **Lines inset toward center, and bigger.** `LineGeometry.qml`'s
  `_edgeAnchor()` used to place each line's endpoint exactly on the
  window's near edge. Slightly overlapping/close windows squeezed that
  point into a thin sliver of shared boundary, making it hard to tell which
  window a line belonged to. Pulled each anchor in by a fixed 28px toward
  the window's center (clamped so it can't cross past center on a small
  window), and bumped the dot/star glyph sizes (10/15px → 14/20px).
- **Super+F/Super+Shift+F re-center the camera on their own axis.** The
  earlier center-anchored resize fix (2026-07-11, below) kept the window's
  *own* center fixed in world space, but never moved the camera — if the
  camera was panned somewhere else, the freshly-resized window could stay
  off-screen or off-center in the viewport. Added
  `viewport_center_on_x()`/`viewport_center_on_y()` (`viewport_ops.c`,
  single-axis variants of the existing `viewport_center_on()`), called from
  `fitwidth()`/`fitheight()` right after the resize settles. Deliberately
  single-axis: `Super+F` only moves the camera horizontally (leaves whatever
  vertical pan the user had), `Super+Shift+F` only vertically — a full
  `viewport_center_on()` would have yanked the untouched axis too.

Verified for real, not by code review alone: rebuilt, 25/25 unit tests,
restarted the live compositor, then drove the actual host session via
`ydotool` + IPC-socket readback (same methodology as the earlier `Super+F`
symmetric-resize verification). Confirmed lines rendering during overview
with a real screenshot (three connected `foot` terminals, dotted/star lines
visible between them, `super_held:false` / `overview:true` in the IPC state
at the same moment). Confirmed axis-only centering numerically: panned the
camera off both axes, pressed `Super+F` — `rect.x` snapped to `0` (window
now fills the monitor width) while `rect.y` stayed exactly where it was;
then `Super+Shift+F` — `rect.y` snapped to `0`, `rect.x` stayed at the `0`
`Super+F` had just set. Did **not** get a clean live confirmation of the
move-in-overview drag itself — synthetic mouse drags via `ydotool` on the
real, `follow`-mode-enabled desktop kept landing on the wrong target as the
camera panned between commands (at one point focused a docked display
panel by accident) — so that one is verified by code review (a one-line
guard mirroring an already-proven pattern in the same function) rather than
a live drag capture. Offered to verify further in the VM if wanted.

## 2026-07-11 — Code audit: dead-code sweep + session-lock extracted out of dwl.c

Requested audit of `dwl.c` (the ~4.5k-line dwl-heritage monolith) for dead
code and pieces cohesive enough to pull into `code/src/modules/`, matching
the decomposition already applied to `viewport/`, `crop/`, `layout/`, etc.
Delegated the survey to an agent (compiler-warning sweep, cross-file grep for
zero-caller functions, `#if 0`/orphan-bind-action checks); acted on its two
findings directly.

**Dead code:** `dwl.c`'s `#ifndef M_PI` guard (top of file) was unused —
confirmed by build warning and grep — leftover from before the
connection-graph/directional-focus angle math that needed it moved to
`modules/layout/connection_graph.c` and `modules/layout/directional_focus.c`,
each of which already carries its own copy. Deleted. Nothing else came back
dead: no `#if 0` blocks, no orphaned `ACT_*` entries (the `unbind`/coverage
machinery added earlier today already guards that), no functions with zero
real callers (a few looked dead to naive grep but turned out to be
wlroots listener callbacks or `bind_actions.c`'s dispatch table).

**Session-lock extraction:** moved the `ext-session-lock-v1` handling
(`createlocksurface`, `destroylock`, `destroylocksurface`, `destroysessionlock`,
`locksession`, `unlocksession`, plus the `session_lock_mgr`/`locked_bg`/
`cur_lock`/`new_session_lock` statics) out of `dwl.c` into a new
`code/src/modules/session_lock.c`, following the same shape as the other
extracted modules. Public surface kept small and dwl.c-initiated (matches
the `wallpaper_configure()`-style pattern): `session_lock_init()` (called
from `setup()`), `session_lock_resize()` + `session_lock_configure_output(m)`
(called from `updatemons()`), `session_lock_cleanup()` (called from
`cleanuplisteners()`), and `destroylocksurface()` staying externally callable
since `cleanupmon()` invokes it directly on a still-open lock surface when a
monitor disappears mid-lock.

Two internal `dwl.c` statics had to be promoted from `static` to real extern
linkage for the new TU to reach them — `dpy` and `motionnotify()`. Both
already had "public" extern declarations sitting in `kalin.h` (unused until
now), which turned out to be the general pattern in this file: `kalin.h`
mirrors *all* of `dwl.c`'s globals/listeners/functions as extern regardless
of whether anything currently uses them externally — most of that mirror is
inert boilerplate, not a signal of actual necessity. Where the new module
didn't actually need a symbol externally (`new_session_lock`, and the
`createlocksurface`/`destroylock`/`destroysessionlock`/`locksession`/
`unlocksession` functions themselves), kept it `static` inside
`session_lock.c` instead and deleted the now-genuinely-dead mirror entry
from `kalin.h` — tighter encapsulation than the original monolith had, not
just a mechanical move. `focus_top(m, lift)` (a `dwl.c`-private one-line
wrapper around `focusclient(focustop(m), lift)`) couldn't be reused since
it's static and used in a dozen other places in `dwl.c`; the two call sites
inside the moved code were inlined to the already-public
`focusclient(focustop(selmon), …)` instead of promoting `focus_top` itself.

Verified for real, not just by compiling: rebuilt (`nix develop -c make
clean all`, no new warnings), 25/25 unit tests pass, live-restarted the real
compositor (clean startup, no `die()`). Session-lock specifically is
security/usability-critical and unsafe to test on the live host session (a
bug could lock the user out with no way for me to unlock it), so instead
added `swaylock` + `security.pam.services.swaylock` to `~/environment/test-vm`
(`vm.nix`), rebuilt the VM against this working tree (`nix flake update
kalin-wm && nix build .#vm`), booted it, and drove a real lock/unlock cycle
through `vmctl.py`: locked with `swaylock -f -c 220044` (screenshot confirmed
the full-screen dim overlay), typed the `tester` password + Enter (swaylock
exited cleanly, desktop reappeared), then typed `echo post-unlock-input-works`
into the terminal and confirmed it echoed — proving keyboard focus was
correctly restored post-unlock, not just that the process didn't crash. No
errors in `kalin-wm.log` across the whole sequence.

## 2026-07-11 — Window-menu (`hold Super`) camera-jump when a docked panel has focus

User reported: "sometimes the panels on bar do move the camera" — an
intermittent, hard-to-pin-down camera jump while interacting with docked bar
panels (e.g. Quickshell-embedded panel terminals docked via `setdocked()`/
the IPC `dock` command).

Traced to `bind_invoke()`'s `ACT_WINDOW_MENU` case (`code/src/dwl.c`,
`hold Super` → window-menu): it fetches `focustop(selmon)` and unconditionally
calls `viewport_menu_reveal(menu_focus)` on the result. `focustop()` filters
by `VISIBLEON()` only — it does **not** exclude `c->ispanel` — so if a docked
panel currently holds keyboard focus (trivially possible: click into a docked
panel to type in it, then hold Super), `menu_focus` is the panel itself.

`viewport_menu_reveal()` (`viewport_ops.c`) treats `c->geom` as world-space
and runs it through a `viewport_pan()` calculation — but a panel's `geom` is
**screen-space**, not world-space (this exact hazard is already documented at
`viewport_ops.c:551-553` on the sibling function `viewport_follow_focus()`,
which *does* guard against it). Feeding a screen-space rect into a
world-space pan calc produces a garbage offset and the camera visibly jumps —
matching the reported symptom exactly, and explaining the "sometimes": it
only fires when a panel (not a normal window) happens to hold focus at the
moment `hold Super` fires.

Every other camera-affecting call site that consumes `focustop()`'s result
already re-checks `!c->ispanel` before touching the camera
(`viewport_follow_focus()`, `viewport_fit_all()`, the new-window auto-pan,
cycle-focus) — this one was the odd one out. Fixed with the same guard:
`if (menu_focus && !menu_focus->ispanel) viewport_menu_reveal(menu_focus);`.
Ruled out as unaffected: `viewport_follow_focus()`, `viewport_fit_all()`,
new-window auto-pan, `setdocked()` itself (doesn't touch focus/camera), and
the overview-mode fit/select paths (panels already excluded from the overview
bounding box and aren't valid click targets there).

Build clean, 25/25 unit tests pass, live-restarted and verified no `die()`
on startup.

## 2026-07-11 — `fitwidth()`/`fitheight()`: grow/shrink from center, not the left/top edge

Immediately after fixing `Super+F`/`Super+Shift+F`'s keybind drift (below), the
user actually exercised the fixed binds and found the *resize behavior*
itself wasn't what they wanted: growing (or shrinking) only extended the
window's right/bottom edge, left/top edge pinned in place, reading as
"resizes to the right" — wanted growth/shrink split evenly on both sides,
center fixed.

`fitwidth()`/`fitheight()` (`code/src/modules/input/resize_actions.c`) only
ever wrote `geo.width`/`geo.height`, never touching `geo.x`/`geo.y` — so
`resize()`'s implicit top-left anchor did exactly what the code said, just
not what was wanted. Fixed by computing the size delta and shifting
`geo.x`/`geo.y` by half of it (`geo.x -= (new_width - old_width) / 2`, same
shape for height) so the center stays fixed instead of the top-left corner.
Deliberately did **not** touch `resizefocused()` (the `Super+=/-`/
`Super+Shift+=/-` keyboard nudge actions) — that one's explicit "keep
top-left anchored for keyboard resizing" comment is a *different*, still
correct interaction: growing a window by small increments while
positioning it against a neighbor wants a fixed corner, not a fixed center.

Verified with real synthetic input (not just code review or IPC-only
checks) via `ydotool` (installed ad hoc through `nix-shell -p ydotool`,
`ydotoold` run against a throwaway `/tmp/.ydotool_socket`, torn down after —
`/dev/uinput` already has a `kalin:rw-` ACL entry, no sudo needed): shrank a
real window with `Super+-` (confirmed `x` stays put, only `width` shrinks —
`resizefocused()`'s anchor is intentionally unchanged), then `Super+F` and
read the new geometry back over the IPC socket's `"rect"` field — `x` moved
by exactly half the width delta in the correct direction (grew 1400→1600,
`x` moved 0→-100, i.e. 100px added to *each* side). Same check for
`Super+Shift+F`/height (1px off from the width case due to integer-division
truncation on an odd delta — expected, not a bug).

## 2026-07-11 — `Super+F` traced to real config drift; bind coverage validation added

User report: `Super+F`/`Super+Shift+F` "not working like expected." Traced,
not guessed:

- **Docs** (`obsidian/keybindings.md`, committed): `Super+F` = fit-width,
  `Super+Shift+F` = fit-height.
- **Compiled default** (`code/config/default_binds.h`) — but only in the
  **uncommitted working tree**: same as docs. The **committed HEAD** version
  is a completely different, much older bind set (`Super+f -> layout
  floating`, `Super+Return -> master-zoom`, `Ctrl+Left/Right -> move-column`,
  etc.) from before the tiling-layout system was ripped out — confirmed via
  `git diff HEAD -- code/config/default_binds.h`, a ~40-line diff. `git log
  -S "fit-width"` found zero commits, because it's never been committed at
  all, only sitting in the working tree (same pattern noted in earlier
  entries — an Explore agent's "is X already done" claims need verifying
  against the actual working tree, not just `git log`).
- **Actual live config** (`~/.config/kalin-wm/binds.conf`, a runtime file
  outside git entirely): `Super+f -> toggle-maximized`, `Super+Shift+f`
  bound to nothing. Root cause: `binds.conf` is written from
  `default_binds.h` **once**, on first run, then never resynced — it's a
  live-editable file the user (correctly, per [[compile-time-config]])
  hand-maintains from then on. At some point `toggle-maximized` was placed
  on `Super+f` (a real, valid action — `bind_actions.c`'s comment on
  `bind_action_is_repeatable()` even references "Super+F" by name from that
  era). Separately and later, in the *uncommitted* working-tree
  `default_binds.h`, `Super+f` got reassigned to the newer `fit-width`
  feature. Nothing ever reconciled the two — `binds.conf` parsed fine
  throughout (both are real actions), so there was no error, no warning, no
  signal anything had drifted. Restored per the user's explicit call:
  `Super+f` → `fit-width`, `Super+Shift+f` → `fit-height` (added, was
  missing entirely), `toggle-maximized` moved to `Super+m` (free, mnemonic).
  Also restored `toggle-overlap`/`link-pick`/`swap-dir`×4/`viewport.pan-grab`
  to `binds.conf` — present in the working-tree default but absent from the
  live file; the user confirmed these were bound during that feature's own
  development and should come back, matching the working-tree default's
  assignments (`Super+Shift+o`, `Super+l`, `Super+Ctrl+Left/Right/Up/Down`,
  `Super+Ctrl+BTN_LEFT`).

**Structural fix, not just a one-off repair** (explicit ask: "forced to
follow developments otherwise it wont start"): a config silently drifting
out of sync with the compositor's evolving action set was never visible as
an error — it just quietly ran with different keybinds than intended,
forever, until someone happened to notice by hand. Added:

- **`unbind <action-name>`**, a new DSL directive (`bind_parser.c`) —
  explicitly declares "I know this action exists, I deliberately don't want
  it on a key," recorded in a new `BindEngine.unbound[ACT_COUNT]` array
  (`binds.h`).
- **`bind_check_coverage()`** (`bind_parser.c`/`binds.h`) — every `ACT_*`
  must be bound somewhere (any mode) or `unbind`-declared, or it fails,
  listing *every* uncovered action in one message (not just the first, so
  fixing is one edit not a fail/fix/reload loop). Deliberately **not** baked
  into `bind_parse()` itself — kept as a separate function so the many
  small, targeted parser unit tests can keep constructing partial DSL
  snippets without also having to enumerate all 37 actions just to satisfy a
  check that isn't what they're testing. `bind_engine.c`'s `binds_load()`
  (the real runtime loader — used for both initial load and hot-reload)
  calls it right after a successful `bind_parse()`.
- **`binds_init()` no longer falls back to the embedded default on
  failure** — it used to (parse the compiled `DEFAULT_BINDS` string and run
  with that instead of the broken user file). Now `die()`s instead: a
  problem with `~/.config/kalin-wm/binds.conf` (bad syntax *or* an
  uncovered action) refuses to start, with the specific problem in the
  message, rather than silently substituting different keybinds than what's
  actually on disk. **Live reloads keep the old, softer behavior** — an
  inotify-triggered `binds_load()` failure logs an error and keeps the
  previous, still-working bind table; only *startup* is now hard-fail, since
  crashing a running session over a bad live edit would be far more
  disruptive than what it's protecting against.
- `code/config/default_binds.h` itself needed two additions to pass its own
  new coverage requirement: `Super+m -> toggle-maximized`, and `unbind mode`
  (named-mode transitions — `mode <name> { ... }` blocks — are supported by
  the engine but nothing in the shipped default actually uses one yet, so
  it's an honest "not wired up" rather than an oversight).
- Tests: `test_shipped_default_parses` now also asserts
  `bind_check_coverage()` on the real embedded default (so a future action
  added to `bind_actions.c` without a matching default bind/unbind fails
  CI-equivalent locally, not silently). Four new tests:
  `unbind_directive`, `unbind_unknown_action`,
  `coverage_catches_missing_action`, `coverage_bind_or_unbind_satisfies`.
  25/25 passing (up from 21).

Verified end-to-end, not just unit-tested: restarted the live compositor
with the corrected `binds.conf` — it started cleanly (no `die()`), meaning
the coverage check passed against the real file, not just a test fixture.

## 2026-07-11 — Multi-monitor bar + read/write display settings & brightness over IPC

Scoped with the user up front (three explicit decisions, since each had a
real cost/tradeoff): **(1)** kept the single shared infinite-canvas camera
model rather than giving each monitor its own independent camera — dwl's
per-monitor tiling/docking already worked, the actual gap was just the bar
only ever appearing on one screen; **(2)** went for full read+write output
control over IPC, not just read; **(3)** brightness moved into the
compositor (not left in the TUI calling `brightnessctl`) via logind's
`SetBrightness`, once direct sysfs writes turned out to be blocked at the OS
permission level on this host (see below).

- **Bar on every monitor** (`quickshell/modules/WindowsBar.qml`): `Variants.
  model` changed from a hardcoded `LVDS-1`-only filter (a deliberate
  single-monitor preference from 2026-06-22, now superseded — see the
  `quickshell-bar-single-monitor` memory) to plain `Quickshell.screens`, so
  every connected monitor gets its own bar + docked panels.
- **DockedPanel app_ids became per-monitor**: `kalin-stats-panel` etc. is now
  `kalin-stats-panel-<screen.name>` (`BottomBar.qml`) — without this, two
  monitors' bars would both try to dock/spawn the *same* named client via
  `client_find_by_appid()`, fighting over one real terminal instead of each
  monitor getting its own. `DockedPanelCoordinator.qml`'s mutual-exclusion
  singleton was rescoped from a single global `activeAppId` to `activeByScreen`
  (a `screenName -> appId` map) for the same reason — opening a panel on one
  monitor must not close a different monitor's open panel.
- **`dockprep`/`dock`/`undock`/`minimize`/`set-output`/`set-brightness`
  IPC commands are now genuinely multi-monitor-safe** as a side effect of the
  above — they were always addressed by app_id/output-name, never assumed a
  single screen, so no compositor-side change was needed here.

### Output read/write over IPC

- **`outputmgrapplyortest()` (dwl.c) already fully implements
  wlr-output-management-v1's apply/test path** — inherited from dwl, not
  something this session added. An earlier research pass (an Explore agent)
  incorrectly reported this as "inert, no listener wired up"; direct testing
  (`wlr-randr --output LVDS-1 --scale 1.25`, read back, reverted) proved it
  genuinely works. Lesson: verify agent research against the actual code/a
  live test before treating it as fact, especially for "is X already done"
  claims — a wrong "not done" claim here would have led to reimplementing
  working code from scratch.
- **New: `ipc_set_output()`** (`dwl.c`, prototyped in `kalin.h`) — the IPC
  equivalent of what `outputmgrapplyortest()` does per-head for an external
  wlr-output-management-v1 client, addressed by output name instead of a
  client-supplied `wlr_output_configuration_v1`. Prefers a real advertised
  mode over a synthesized `custom_mode` when width/height/refresh match one
  (same preference the real protocol path gives a client-supplied mode),
  falling back to `custom_mode` otherwise. `width`/`height` `<=0` leaves the
  mode unchanged, `scale<=0` leaves scale unchanged — a caller that only
  wants to reposition or disable an output doesn't need to already know its
  current mode/scale just to pass them through untouched.
- **New: `monitor_find_by_name()`** (`dwl.c`) — trivial `mons` list lookup by
  `wlr_output->name`, the output-side equivalent of `client_find_by_appid()`.
- **State broadcast gained `"outputs":[...]`** (`ipc.c`) — every connected
  monitor's name/position/current mode/scale/enabled *and full advertised
  mode list*, so a TUI can build a real resolution picker without shelling
  out to `wlr-randr`. `IPC_BUF_SIZE` bumped 4096→8192 to fit it comfortably
  alongside the existing connection-graph payload.
- **New command: `set-output <name> <w> <h> <refresh> <scale> <x> <y>
  <enabled>`** (`ipc.c`) — thin wrapper around `ipc_set_output()`.

### Brightness: compositor-side, via logind, not sysfs or brightnessctl

- **Real finding, not assumed**: `/sys/class/backlight/intel_backlight/
  brightness` is `root:root` with no group-write bit on this host, and there
  is no udev rule granting the `video` group write access (checked — none
  found). Confirmed by testing `brightnessctl set 90%` directly: it failed
  with "Invalid request descriptor," meaning the *existing* Textual display
  panel's brightness control was already broken here before this session,
  for a reason with nothing to do with kalin-wm's own code.
- **Fix: logind's `SetBrightness` D-Bus method**, not a udev-rule system
  change (offered both options; chose this one — no `nixos-rebuild switch`
  needed, and it's the standard modern-Linux path for this rather than a
  permissions workaround). New file **`code/src/modules/backlight.c`**:
  `backlight_get()`/`backlight_set()`, linking `libsystemd` (sd-bus) — added
  as an explicit `flake.nix`/`Makefile` dependency (`SD_FLAGS`/`SD_LIBS`),
  it was only incidentally on `pkg-config`'s search path inside `nix develop`
  before, not a real declared build input, so a from-scratch/sandboxed Nix
  build would have failed to link.
- **Real bug hit and fixed**: `GetSessionByPID(0)` (resolves "the session
  the calling process belongs to") failed with "Caller does not belong to
  any known session" when kalin-wm was relaunched via a detached `nohup`
  shell during this session's own testing — that process tree genuinely
  isn't part of any systemd-logind session. Added a fallback: `ListSessions`
  + pick the first entry with a non-empty `seat_id` (a real seated session,
  not a headless `systemd-user` manager scope). Confirmed this really is a
  testing-methodology artifact, not a design flaw: `SetBrightness` itself
  still failed ("Session is not in foreground, refusing") even with a
  correctly-resolved *target* session path, because logind's authorization
  checks the *calling process's own* session via its D-Bus connection
  credentials — no `session_path` argument can substitute for the caller
  itself genuinely being part of an active foreground session. Once the user
  relaunched kalin-wm normally from their own real login-session terminal
  (not a detached shell), both the resolution and the actual `SetBrightness`
  call worked immediately, no further code changes needed.
- **State broadcast gained `"brightness":{"value":<raw>,"max":<raw>}|null`**
  (`ipc.c`) — raw sysfs-style values (not percent), `null` if no backlight
  device exists (e.g. a desktop with no built-in panel).
- **New command: `set-brightness <value>`** (`ipc.c`) — raw value, wraps
  `backlight_set()`.

### `tools/display-panel/display_panel.py`

Rewrote the kalin-wm backend path to talk to `$KALIN_IPC_SOCKET` directly —
`query_outputs_kalin_ipc()`/`set_output_mode_kalin()`/
`set_brightness_kalin()` — replacing the old `wlr-randr --json` +
`brightnessctl` shellouts entirely for that path (niri's path is untouched:
still `niri msg --json outputs` + `brightnessctl`, since none of this
session's compositor-side work applies there). Added a `m` keybind to cycle
the first output through its advertised modes via `set-output`, alongside
the existing brightness up/down keys (now routed to `set-brightness` under
kalin-wm, `brightnessctl` under niri). Verified end-to-end against the live
compositor: reading (`outputs`+`brightness` both populate correctly,
`merge_kalin_brightness()` correctly pairs the backlight to `LVDS-1`) and
the full Textual app rendering (`kalin-ipc · 1 output(s)`, `LVDS-1 1600x900
@ 60.06Hz`, brightness bar at the real live percentage) both confirmed by
direct test run, not just code review.

## 2026-07-11 — `Super+O` spam-toggle camera wiggle: `toggle_overview()` saved the wrong viewport field

Found immediately after the `ispanel`/overview fix below, while re-testing it:
rapidly toggling `Super+O` (overview) made the camera settle at a visibly
different position each time instead of returning cleanly to where it
started — a small wiggle that got worse the faster you spammed the key.

`toggle_overview()` (`overview.c`) saved `viewport.x/y/zoom` as the "restore
to this on exit" point. Those are the **animated, currently-interpolating**
fields (see `viewport_camera_tick()`'s smooth-pan step) — `viewport.
target_x/y/zoom` are the actual destination the pan is headed toward.
Toggling faster than a pan animation settles sampled `.x/.y/.zoom` at a
different point along that in-flight interpolation each time, so each
toggle-back restored to a slightly different "pre-overview" position than
the last. Fixed by saving `viewport.target_x/y/zoom` instead — stable
regardless of animation progress, since `overview_exit()` itself just sets
`target_x/y/zoom = saved_x/y/zoom` and a subsequent re-entry reads that same
already-settled value straight back, with no dependency on how far the
animation had actually gotten. `viewport_fit_all()` itself needed no change:
it computes the fit purely from client geometries and monitor size, with no
dependency on the camera's current position, so it was already idempotent.

## 2026-07-11 — `c->ispanel`: a permanent compositor-wide tag for docked panels; `dockprep` IPC command

Found on the real host (the VM never surfaced this — no real hardware to make
nmtui/bluetuith stick around long enough to interact with, so this class of
bug never got triggered there) after the previous entry's panel rollout:
opening a docked panel visibly dragged the camera toward the bottom-right,
and `Super+O` (overview) framed the wrong area whenever a panel was open.

Root cause: every "does this client count as a real navigable window"
call site — camera-follow, fit-all/overview, the wlr-foreign-toplevel-
management taskbar feed, directional focus (`Super`+arrow), focus cycling
(`Super+Tab`) — was treating a docked panel's `c->geom` as a normal world-
space window. For a docked client `c->geom` is a **screen-pixel** rect (see
`setdocked()`), so e.g. `viewport_follow_focus()` calling
`viewport_ensure_visible()` on a focused panel (which happens just from
clicking into it to type) panned the camera toward wherever that rect
happens to sit on screen — bottom-right, matching every panel's placement.
`viewport_fit_all()` (shared by `Super+0` and `toggle_overview()`) folded the
same rect into its world-space bounding box, skewing both.

**`c->docked` alone isn't a reliable guard for this** — it's *transient*:
`DockedPanel.qml`'s `_close()` calls `undock()` before `minimize(true)`, so a
closing panel passes through a brief undocked-but-about-to-hide window where
`c->docked` is false but it's still emphatically not a real window to camera-
follow, list on a taskbar, or Tab onto.

Added `c->ispanel` (`kalin.h`): set once, permanently, the first time
`setdocked()` ever docks a client (`docked` 0→1) — never cleared by a later
undock. This is the actual "is this shell-panel chrome" tag; `docked` stays
what it was (a *current-state* flag driving the screen-space render
transform, correctly still transient). Guarded on `!c->ispanel`:
`viewport_follow_focus()`, `viewport_fit_all()` (so both `Super+0` and
`Super+O` are fixed), `ftl_create()` (so a panel never gets a
foreign-toplevel handle — the earlier fix already made `mapnotify()`'s
`follow_new_windows` auto-pan skip it too, now via `ispanel` instead of
`docked` for the same undock-window reason), `focusstack()`
(`Super+Tab`), `directional_focus.c`'s nearest-window search
(`Super`+arrow), and `offscreen_indicators.c` (redundant given a docked
client's actual scene position is always genuinely on-screen, but cheap and
matches the "compositor-wide" framing). Connection-graph code needed no
changes — a panel never gets a `neighbor[]` edge in the first place (see
below), so every graph-walking function already skips it by construction.

**Also added: `dockprep <appid> <x> <y> <w> <h>` IPC command** (fixes a
separate but related symptom — a panel's *first-ever* spawn briefly flashed
at a normal floating position before the shell's `dock` command caught up,
and that flash's placement was *also* what dragged the camera via the
`follow_new_windows` auto-pan). `DockedPanel.qml` now calls
`KalinViewport.dockPrep(appId, x, y, w, h)` right before spawning a panel's
backing terminal for the first time — the compositor remembers this as a
one-shot pending request (`dockprep_register()`/`dockprep_consume()` in
`dwl.c`, a small fixed-size array; keyed by app_id since the shell has no
numeric client id for a process it hasn't spawned yet). `mapnotify()`
consumes it the moment a matching app_id maps: skips the entire normal
placement/persistence/spawn-connection-graph dance and calls
`setdocked(c, 1, rect)` directly, so the client is docked from its very
first rendered frame — never visible anywhere else, and (since `setdocked()`
sets `c->ispanel` immediately, before `mapnotify()` reaches the
`follow_new_windows` check later in the same call) never drags the camera
either. `DockedPanel.qml` still runs its existing `firstSpawnDelay` timer's
`dock()`/`minimize(false)` afterward as a fallback — harmless no-ops once
`dockprep` has already taken effect, still load-bearing if a future app_id
mismatch ever makes `dockprep` miss.

Both fixes verified 21/21 unit tests green + full build clean; live
verification (camera stays put on panel focus, `Super+O` frames correctly
with a panel open, no flash on first spawn) pending a compositor restart on
the real host.

## 2026-07-10 — wired all five right-side panels to DockedPanel; mutual-exclusion coordinator

Completed the rollout the previous two entries laid groundwork for: every
right-side bar panel (stats, wifi, bluetooth, volume, display, plus the
already-shipped clipboard) is now a real, riced TUI app docked via
`DockedPanel.qml`, replacing QML-rendered content or ad hoc floating-window
launches. `SystemPanel.qml` now only holds the Battery pane (no TUI
replacement — it's a live UPower readout, not a launchable app); `StatsPanel`,
`MixerPanel`, `DisplayPanel`, and `widgets/TuiPanel.qml` were deleted as fully
orphaned (grepped first to confirm zero remaining references).

- **`BarConfig.tuiPanelWidth`/`tuiPanelHeight`** (700×480, new — separate from
  `panelWidth`/`panelHeight` at 440×520): `btop` hard-refuses to render below
  80×24 cells and prints "Terminal size too small" instead of its UI. The
  QML-drawer sizing (440px) only fit ~51 columns at foot's default font, well
  short of 80. `DockedPanel.qml`'s size properties now default to the TUI
  constants instead.
- **`DockedPanelCoordinator.qml`** (new singleton): every `DockedPanel`
  instance renders into the *same* on-screen rect (bottom-right, flush with
  the bar), so two open at once would visually fight for the same spot. A
  panel calls `claim(appId)` right before opening; if a different panel was
  active, the coordinator fires `closeRequested` at it first (unpins it,
  clears its grace timer) before granting the new one the slot — the docked
  equivalent of the old SidePanel drawer only ever showing one tab.
- **Widget rewrite**: `WifiLauncher.qml`/`BluetoothLauncher.qml` replaced the
  old `WifiWidget`/`BluetoothWidget` (deleted), now thin wrappers around a new
  shared `TuiLauncherWidget.qml` (icon-only bar button, min-width 48,
  `tabName` prop for icon binding) — same shape as `SystemStatsWidget`/
  `VolumeWidget`/`DisplayWidget`, just without their own inline percent text.
- **Display panel**: dispatched to an agent to build a Textual (Python TUI)
  app from scratch (`tools/display-panel/display_panel.py`, packaged in both
  `home-config` and `test-vm` via `pkgs.python3.withPackages (ps: [ps.textual])`),
  replacing `DisplayPanel.qml`. Auto-detects backend: `wlr-randr` under
  kalin-wm (confirmed working — kalin-wm implements
  `wlr-output-management-v1`), `niri msg --json outputs` under niri (niri does
  **not** implement that protocol, confirmed via `home-config/display.nix`'s
  own portal comment — the inverse of the naive assumption that niri would be
  the one with better Wayland protocol support).

**Debugging note, since it ate most of this session**: wifi and bluetooth
panels *looked* broken (clicking/hovering the bar button did nothing
observable) through a chain of three unrelated causes, each masking the next:

1. `test-vm`'s `quickshell-config` flake input is `path:`, which behaves like
   `git+file:` — it locks to a narHash snapshot and needs
   `git add -u` (staging, not committing) in `~/environment/quickshell` +
   `nix flake update quickshell-config --allow-dirty-locks` in `test-vm`
   before a rebuild picks up working-tree changes at all. Same gotcha
   documented for kalin-wm's own flake input in an earlier entry; forgot it
   applies to quickshell-config too. Without this, the VM was running a
   build that predated `WifiLauncher`/`BluetoothLauncher`/`DockedPanel`
   entirely, so wifi/bluetooth simply weren't in the row — no error, just
   silently absent, and every other panel's positions shifted left to fill
   the gap, which produced a very convincing false trail (clicking where
   wifi "should" be keep hitting battery/volume instead).
2. Once rebuilt: `nmtui`/`bluetuith` both **hard-exit within milliseconds**
   if their D-Bus service isn't reachable (`NetworkManager is not running`,
   `Could not activate remote peer 'org.bluez': unit failed`) — `test-vm`'s
   `vm.nix` had only added the `networkmanager`/`bluez` **packages**, never
   enabled the actual services (`networking.networkmanager.enable`,
   `hardware.bluetooth.enable`). The docked `foot` window spawns and dies
   before a `ps -ef` check (even a fast one) can catch it, which reads
   identically to "the click never registered." Fixed by enabling both
   services in `vm.nix`.
3. Even with both services enabled, **`bluetooth.service` still won't start
   in this VM** — `journalctl -u bluetooth` shows `Bluetooth service
   skipped, unmet condition check ConditionPathIsDirectory=/sys/class/bluetooth`,
   because QEMU doesn't emulate a Bluetooth controller by default, so
   `/sys/class/bluetooth` never exists. This is a hard VM-environment
   limitation, not a code bug: confirmed via temporary debug instrumentation
   (bright always-on border colors on the launcher widgets +
   `console.log` in `DockedPanel._open()`/`togglePin()`, all reverted before
   the final build) that the click/hover chain reaches `BluetoothLauncher`,
   calls `_open()`, and spawns `foot --app-id=kalin-bt-panel -e bluetuith`
   correctly — `bluetuith` itself is what exits, for the same reason it
   would if run by hand at a shell prompt. The real host has real Bluetooth
   hardware, so this only affects VM testing.

Verified end-to-end in the VM after all three fixes: stats→`btop`,
wifi→`nmtui` (interactive, drilled into "Edit a connection"),
volume→`wiremix` (interactive, tab bar responsive), display→the Textual app,
clipboard→the existing picker loop — all dock, show live content, and
correctly hand off the shared screen rect via the coordinator (opening one
reliably closes whichever other panel was pinned). Bluetooth verified up to
the point VM hardware allows: correct spawn of the correct command.

## 2026-07-10 — generalized DockedPanel component (quickshell)

- Extracted the clipboard panel's spawn/dock/undock/minimize state machine
  into a reusable `modules/DockedPanel.qml`, so the remaining right-side
  panels (stats, volume, wifi, bluetooth, display) can each get one instance
  instead of copy-pasted logic. Removed the now-redundant
  `ClipboardPanelState.qml` singleton and the `clipboardClicked()` signal
  indirection through `WindowsBarScreen.qml` — the panel now lives entirely
  in `BottomBar.qml`, next to its button, since that's where `screen`/
  `heightHint` already are.
- Added hover-driven open/close, matching the existing QML `SidePanel`
  drawer's behavior instead of click-only: `buttonHover` (bound to the bar
  button's `hovered`) or `panelHover` (`KalinViewport.dockHoverAppId ===
  appId`, from the new `dock_hover` IPC field above) opens it; losing both
  starts a 220ms grace timer (same constant `WindowsBarScreen.qml` already
  uses for its own drawer) before closing, so moving the cursor from the
  button to the panel doesn't flicker it shut. Click still pins it open
  regardless of hover, same as the clock button.
- **Real bug caught by the VM boot log, not by lint**: `DockedPanel.qml`'s
  `required property ShellScreen screen` failed at runtime
  (`ShellScreen is not a type`) because the file only imported
  `Quickshell.Io`, not the base `Quickshell` module `ShellScreen` actually
  lives in — `qmllint` didn't catch this (its import-resolution warnings are
  noisy/expected across this whole codebase, confirmed by linting known-good
  existing files and seeing the identical class of warnings), so the VM's
  own `quickshell.log` was the thing that actually caught it.
- Verified in the [[test-vm]]: hovering the bar button (no click) spawned
  the terminal, docked it, and it appeared — confirmed via the guest's
  process list *and* a screenshot showing the live `fzf` session. Moving the
  cursor away closed it (confirmed via screenshot — panel gone) while the
  process tree stayed fully alive (confirmed via `ps -ef`: `foot → bash →
  kalin-clip-picker-loop → bash → kalin-clip-picker → fzf`, all still
  running). One test-harness note: the VM's synthetic pointer needs
  noticeably more real wall-clock time to actually trigger a hover-open than
  a click does — an early screenshot after `vmctl move` can read as "hover
  doesn't work" when it's actually still catching up; wait ~1.5s+ before
  concluding a hover interaction failed.

## 2026-07-10 — dock-hover IPC events + animated dock reveal

Foundation for converting every right-side panel (stats/volume/wifi/
bluetooth/display, not just clipboard) to the same real-terminal docking
pattern, matching the existing SidePanel drawer's hover-to-open,
auto-hide-on-leave, animated-reveal behavior:

- **`dock_hover` IPC field** (`dwl.c`/`ipc.c`): a new `Client
  *dock_hover_client` global, updated in `motionnotify()` right where
  `xytonode()` already resolves the client under the cursor — compares
  against the previous value and only broadcasts on an actual enter/leave
  transition (not every motion tick, which would flood the socket). Mirrored
  into the state JSON as `"dock_hover":"<appid>"|null`. Cleared safely in
  `unmapnotify()` if the hovered client is the one closing, mirroring the
  existing `connect_pick_pending()` dangling-pointer guard right above it in
  the same function — same class of bug, same fix shape.
- This exists because a docked client is a **real Wayland toplevel**, not
  QML content — the shell has no way to observe "is the cursor over this
  window" on its own the way it can for a QML `HoverHandler`. The
  compositor is the only thing that knows.
- **Animated dock reveal** (`setdocked()` in `dwl.c`, plus a bugfix in
  `client_anim.c`): docking now glides into place via the existing
  spring-glide system (`client_set_target_geom()`, stiffness=250/damping=26,
  same constants used for every other window-move animation) instead of
  teleporting. The glide's *start* point is synthesized as directly below
  the target rect (same x/width/height, y at the rect's bottom edge — right
  at the bar) rather than the client's actual pre-dock position, which is in
  **world space** while the dock rect is **screen space**; animating
  between the two directly would've produced a nonsensical diagonal swoop.
  Undocking doesn't animate — every current caller pairs it with an
  immediate `minimize`, so there's nothing on screen to see it glide.
- **Real bug caught by this work**: `clients_anim_step()`'s cheap per-frame
  position update unconditionally applied the `WORLD_TO_SCREEN` camera
  transform, which is correct for normal (world-space) clients but would
  have **double-transformed** a docked client's already-screen-space
  geometry every frame during the glide — the final settle (`resize()`) was
  fine since it already routes through `client_apply_zoom_frame()`'s
  existing fullscreen/maximized/docked bypass, but the animation itself
  would have visibly swooped through the wrong part of the screen before
  snapping correct. Fixed by adding the same `isfullscreen || ismaximized
  || docked` bypass to this second, previously-undiscovered call site.
- Verified in the [[test-vm]]: final settled position/size/borderless state
  confirmed correct (including a false alarm — the blue outline visible
  right after docking turned out to be the *focus ring* on the
  newly-focused client, not a border; confirmed by clicking elsewhere and
  watching it disappear while the docked client stayed borderless). **Did
  not** manage to visually capture the glide itself mid-flight — the spring
  settles in well under a second and the screenshot-polling VM test harness
  couldn't reliably win that race. Confidence in the animation itself rests
  on code review (the coordinate-space fix, the reused/already-proven settle
  path) rather than a captured frame — flagging this honestly rather than
  claiming a verification that didn't actually happen.

## 2026-07-10 — Display settings panel: Textual TUI (not yet docked)

- Added `tools/display-panel/display_panel.py`, a standalone Textual TUI
  replacing the QML `DisplayService`/`DisplayWidget` pair
  (`~/environment/quickshell/modules/services/DisplayService.qml`), matching
  the docked-terminal pattern used by the [[quickshell-shell]] clipboard panel
  above. Not wired into any docking yet — that lands separately once a new
  docking mechanism is in place; this is just the app.
- Confirmed by hand against the live kalin-wm session: kalin-wm registers
  `wlr_output_manager_v1` (`code/src/dwl.c`), so `wlr-randr --json` **does**
  work under kalin-wm (correctly listed `LVDS-1`, 1600x900@60Hz, scale 1x) —
  unlike niri, which per `~/home-config/display.nix`'s portal comment does
  *not* implement wlr-output-management. So the panel picks its backend the
  opposite way one might guess: `wlr-randr` under kalin-wm (detected via
  `$KALIN_IPC_SOCKET`, mirroring `KalinViewport.enabled`), `niri msg --json
  outputs` under niri, and a clear "unavailable" state if neither applies.
- Brightness uses `brightnessctl`, same as the QML version, and is
  compositor-agnostic.
- Deliberately dropped from the QML version: display reordering and
  resolution/scale cycling, both niri-only (`niri msg output ... position
  set/mode/scale`) and not something kalin-wm can do yet (no output
  IPC — confirmed against `code/src/modules/ipc.c`, which only exposes
  camera/window-docking commands).

## 2026-07-10 — Quickshell docked clipboard panel (bar button, ships the plan)

- Completes the feature the docking primitive + loop wrapper were built for:
  a bar button in the [[quickshell-shell]] that docks/undocks a real,
  fully-interactive `kalin-clip-picker-loop` terminal at a fixed spot in the
  bar's layout — no QML-rendered terminal, no new Wayland protocol, just the
  compositor positioning a real client where the shell says. See
  [[quickshell-shell]]'s new section for the full writeup (files touched,
  the `Process.command` declarative-vs-imperative bug hit and fixed, and the
  VM verification).
- kalin-wm side: one more small IPC command, `minimize <appid> <0|1>`
  (wraps `setminimized()`), needed so the panel can fully disappear on close
  and reappear already-running on reopen — dock/undock alone only
  repositions, doesn't hide.
- Full open → close → reopen cycle verified in the [[test-vm]] via the
  guest's own process list — real spawn only on first open, hidden-not-killed
  on close, same session resumed on reopen.
- Not yet built via `home-config`/NixOS — `kalin-clip-picker-loop` isn't on
  the real host's PATH until that rebuild happens, same as the earlier
  clipboard-history-TUI work this depends on.

## 2026-07-10 — clipboard picker loop wrapper (kalin-clip-picker-loop)

- `kalin-clip-picker-loop` (`home-config/desktop.nix`, next to `clipPicker`):
  `while true; do kalin-clip-picker; done` with a `trap 'exit 0' TERM INT` so
  a real kill still stops it. For the planned docked clipboard panel: the
  panel opens/closes by docking/undocking-and-minimizing one long-lived
  terminal (see the docking-primitive entry above), not by spawning a fresh
  process each time, so it must always have a *live* fzf session ready
  instantly — never a dead shell prompt left over from the last selection.
- Verified functionally on the host (not just built): spawned `foot --app-id
  =loop-test -e kalin-clip-picker-loop`, selected an entry (a 2MiB PNG) with
  Enter, and confirmed both that `wl-paste --list-types` showed `image/png`
  immediately after (the selection actually copied) *and* that the terminal
  showed a fresh `fzf` session with the full 491-entry list back up within
  about a second, rather than exiting to a bare shell prompt.
- Next: the Quickshell panel itself — bar button computes its popup's
  on-screen rect and sends `dock`/`undock kalin-clip-panel` (spawning
  `foot -e kalin-clip-picker-loop --app-id=kalin-clip-panel` on first open)
  over the existing IPC socket.

## 2026-07-10 — compositor-level window docking primitive (IPC dock/undock)

- New primitive for a planned Quickshell feature: real, interactive terminals
  (or any client) embedded at an exact screen position inside a shell panel,
  without rendering a terminal emulator in QML. Since kalin-wm already
  controls compositing, a client can just be *positioned* into the panel's
  layout region rather than having its pixels re-rendered by the shell —
  input routing is free, since it's a genuine toplevel occupying that screen
  region.
- `Client.docked` (`kalin.h`) + `setdocked(Client *c, int docked, struct
  wlr_box rect)` (`dwl.c`, mirrors `setfullscreen()`'s shape: save/restore
  `c->prev`, force `bw`, reparent, `resize()`) — forces borderless and exempts
  the client from the world/camera transform in `client_apply_zoom_frame()`/
  `client_set_buffer_scale()`, the same bypass fullscreen/maximized already
  use, so a docked window stays glued to its panel position regardless of
  infinite-canvas pan/zoom underneath it.
- New IPC commands (`ipc.c`): `dock <appid> <x> <y> <w> <h>` and `undock
  <appid>`, addressed by app_id (a shell panel already knows the app_id it
  spawned). Added `client_find_by_appid()` as a shared lookup and refactored
  `togglescratchpad()` to use it too (was an inline duplicate of the same
  loop).
- Also shipped `kalin-dock`/`kalin-undock` (`home-config/desktop.nix`, next
  to `kalin-clip-picker`): thin `writeShellScriptBin` CLI wrappers around the
  IPC dock/undock commands (`kalin-dock <appid> <x> <y> <w> <h>` /
  `kalin-undock <appid>`), using python3's stdlib for the raw `AF_UNIX`
  socket write rather than `nc`/`socat` — netcat's `-U` flag isn't portable
  across implementations and neither is reliably present.
- Verified in the [[test-vm]]: spawned a `foot --app-id=win-a` window, sent
  `dock win-a 100 400 500 300` over the real IPC socket (via a small Python
  script dropped through the VM's shared-folder mount, since `socat` isn't in
  the VM image and typing raw shell pipelines through synthesized keystrokes
  kept mangling quoting — the VM had to be rebooted once mid-session after
  a garbled multi-window spawn command left an app_id unset and produced a
  false-alarm "z-order bug") — the window jumped to that exact rect, border
  gone. Then spawned a second normal window (`win-b`) deliberately overlapping
  the docked rect: the docked window's content stayed fully visible through
  the overlap (confirmed via a pixel crop of just that region), i.e. it
  correctly renders **above** normal windows, not just borderless/positioned.
  Also clicked into a docked window and typed into it directly, confirming
  it's a real, fully interactive toplevel, not a texture. `undock` then
  restored the original position, size, and border exactly.
- Also retested on the **real host session** (not just the VM), twice. The
  first pass looked like a z-order bug (docked window appearing to render
  *below* the browser on top of it) — resolved on the second, cleaner pass:
  cropping exactly the dock target rect with `grim -g` showed the docked
  window's own text legibly blended with the browser's text behind it,
  which traced to `~/.config/foot/foot.ini`'s `alpha=0.88` — the user's foot
  theme is intentionally 12% transparent. The docked window was correctly
  topmost the whole time; alpha blending just made it look otherwise at a
  glance. Not a compositor bug. (The very first host attempt was also
  probably this same effect, not a separate issue.)
- Not yet wired into anything user-facing — this is the compositor-side
  primitive only. Next: a `kalin-clip-picker`-loop wrapper (restarts `fzf`
  after every selection so it's always "loaded"), then the Quickshell panel
  itself (bar button, computes its popup's on-screen rect, sends
  `dock`/`undock` over the existing IPC socket on open/close).

## 2026-07-10 — clipboard-history TUI + found/fixed a silent binds.conf fallback bug

- Added `Super+V`: opens a foot terminal running `kalin-clip-picker`, an fzf
  TUI over `cliphist` (list history, live preview, select to re-copy). The
  picker itself is a `writeShellScriptBin` in `home-config/desktop.nix`
  (`clipPicker`), not kalin-wm C code — kalin-wm only owns the keybind
  (`code/config/default_binds.h`: `bind Super+v -> spawn foot -e
  kalin-clip-picker`). `cliphist`/`fzf`/`wl-clipboard` were already installed
  on the host; the watcher (`wl-paste --watch cliphist store &`) needs to run
  in-session — currently started manually, not yet wired into kalin-wm's own
  startup command (`-s` flag). Follow-up: bake the watcher into the standard
  startup string once a canonical one exists.
- While debugging why `Super+V` "did nothing" on the real host, found that
  `~/.config/kalin-wm/binds.conf` had been failing to parse *in its entirety*
  for an unknown but apparently long stretch of time: it still had several
  binds referencing actions removed long ago (`master-zoom`, `layout
  infinite/floating/toggle`, `toggle-floating` ×2, `move-column` ×2,
  `move-window` ×4) — leftovers from before the connection-graph rewrite.
  Because `bind_parse()` is all-or-nothing, the *entire* file was silently
  rejected on every load, and kalin-wm was falling back to its compiled-in
  `DEFAULT_BINDS` the whole time. This is exactly the kind of silent-fallback
  failure mode the project's vault-discipline section warns about — it masked
  itself as "screenshot-ui works" (present in the compiled fallback from an
  earlier rebuild) vs. "Super+v doesn't" (added to defaults *after* that
  rebuild, so absent from the stale compiled-in copy the running process had
  in memory).
- Fixed by deleting all the dead lines from the live config (confirmed via a
  small standalone harness linking `bind_parser.c`/`bind_actions.c` directly
  against the real file) until it parsed cleanly, then rebuilt kalin-wm so
  the compiled-in fallback matches current reality too. Per explicit
  instruction: no compatibility shims, no preserved dead binds "just in
  case" — treat `binds.conf` + the bind DSL as the single source of truth
  going forward, don't leave dead code/config lying around masked by a
  fallback.
- Verified end-to-end on the **real host session** (not the VM) using `grim`
  (screencopy) for screenshots and `wtype` (virtual-keyboard protocol) to
  synthesize `Super+V`/`Escape`, since this is the actual running desktop,
  not something scriptable via QMP. Confirmed the picker opens, shows live
  history (489 entries), and closes on Escape.

## 2026-07-10 — niri-style interactive screenshot UI (Super+Shift+S)

- Added a [[screenshot-ui]]: `Super+Shift+S` opens a dim overlay pre-selecting
  the whole focused monitor (drag to draw a custom region instead), with
  niri's exact key scheme — Escape cancels, Space/Enter confirms to disk +
  clipboard, Ctrl+C confirms clipboard-only, P toggles pointer visibility.
  Reverse-engineered from niri's `src/ui/screenshot_ui.rs` (behavior, not a
  literal port — C/wlroots vs. Rust/smithay); reuses [[crop-mode]]'s overlay
  visual pattern (dim rect + bright `wlr_scene_rect` border on `LyrOverlay`).
- New files: `code/src/modules/screenshot/screenshot_ui.c` (UI state/drag
  selection), plus `capture.c` grew `capture_export_selection()` (crop +
  PNG-encode a selection from a refactored `capture_render_native()` shared
  with the existing full-monitor `Super+Print` capture) and a keypress
  intercept block in `input/keyboard.c` mirroring crop-mode's bare-key
  pattern.
- Hit and fixed a real compositor-hang bug during VM verification: the first
  clipboard implementation piped PNG bytes into `wl-copy`'s stdin
  synchronously from the compositor's own process, which deadlocks (wl-copy
  needs to round-trip over Wayland with the same compositor to register the
  clipboard source, but the event loop was blocked inside `write()` on a
  multi-MB pipe that could never drain). Reproduced live: pointer motion
  stopped updating the VM's framebuffer entirely after a capture. Fixed by
  always writing the PNG to disk (real save path or a `$XDG_RUNTIME_DIR` temp
  file) and handing wl-copy a *path* via a detached, non-blocking
  `fork()+exec("sh -c 'wl-copy < path; rm -f path'")` — no image bytes ever
  pass through the compositor process. See [[screenshot-ui]] for the full
  writeup.
- Verified end-to-end in the [[test-vm]]: build + 18 unit tests green,
  booted the VM, drove `Super+Shift+S` → Space via QMP, confirmed the saved
  PNG in `~/Pictures/Screenshots/`, confirmed `wl-paste --list-types` shows
  `image/png`, and confirmed the compositor stayed responsive after capture
  (pointer-move screenshots differ, ruling out the earlier hang). Did not
  separately drive the drag-custom-region, Escape-cancel, or Ctrl+C-clipboard
  paths in the VM — those share the same confirm/cancel code paths already
  exercised and passed the build's `-Werror` type/signature checks, but
  weren't independently clicked through.
- Did not activate the real host login session; VM-only per this project's
  guardrail.

## 2026-07-10 — forgiving connection severing + menu-armed connection creation

- Two [[connection-graph]] pain points: severing required a pixel-precise
  click on a thin dotted line, and there was no way to *create* a connection
  manually at all (only automatic, spawn-adjacency/splice paths).
- Researched prior art before designing: no mainstream WM has an equivalent
  feature (niri/PaperWM are scrollable-tiling, not a persistent adjacency
  graph); the real precedent is node-graph editors (Houdini, Unreal
  Blueprint) — drag-to-wire to create, hold+drag-across to cut.
- **Sever**: `buttonpress()`'s one-shot `connection_click_hit()` check became
  a new `CurCut` cursor mode (`kalin.h`) that `motionnotify()` re-tests every
  tick for the rest of the drag — sweeping the cursor near/across a line now
  cuts it, not just a precise click on it.
- **Create**: new `Super+L` / `link-pick` bind (`ACT_LINK_PICK`) arms the
  focused window as a pending connection source
  (`connect_pick_arm/cancel/complete/pending()`, `connection_graph.c`); the
  next click on a different window completes it via the existing
  `connect_clients()`. A literal QML drag handle wasn't viable (the
  hold-Super menu overlay is deliberately click-through, and partial input
  regions for lines never receive clicks on this wlroots/Quickshell
  combination — already learned the hard way for click-to-sever), so it's
  key-armed instead, with a new "Link" button in `WindowActions.qml` and a
  live rubber-band line (`ipc.c`'s new `"pending_connect"` field,
  `ConnectionLines.qml` reusing its existing line renderer) tracking the
  cursor while armed.
- Added `connect_pick_complete()`'s guard-logic tests to
  `test_connection_graph.c` (3 new cases); `make clean all` + `make
  test-unit` both clean throughout.
- VM-verified all three behaviors end-to-end (`~/environment/test-vm`,
  extending `vmctl.py`'s QMP wrapper with raw key-down/up and drag-button
  events it didn't previously expose, since the built-in `key`/`click`
  helpers only send whole press+release chords/clicks): the VM's persistent
  disk had a stale `~/.config/kalin-wm/binds.conf` from an earlier boot
  missing the new bind (user config takes priority over compiled defaults
  once written) — deleted `kalin-test.qcow2` for a clean rebuild after
  confirming with the user, since it's destructive to that disk's state.
  Screenshots confirmed: Link button appears and highlights when armed, the
  rubber-band line tracks the live cursor, clicking a target completes the
  link and clears the armed state, and dragging across an existing
  (imprecisely-targeted) line severs it.

## 2026-07-10 — verified hover-focus/camera-follow behavior, no code change needed

- Requested behavior: hovering over a different window switches focus
  instantly, camera glides to follow rather than snapping.
- Traced the path (`motionnotify()` → `pointerfocus()` → `focusclient()` →
  `viewport_follow_focus()` → `viewport_ensure_visible()` →
  `viewport_move_to(nx, ny, 1)`) and confirmed all the defaults needed for
  this are already on: `sloppyfocus = 1` (instant focus switch on hover),
  `viewport.follow = 1` and `viewport.smooth_pan = 1` (animated camera
  follow, not a snap). No runtime code path turns `smooth_pan` off.
- No code changed; `make clean all` re-verified green. Gap was documentation
  only — [[follow-mode]] didn't previously describe the hover path at all —
  fixed by adding a "Hover: instant focus switch, animated camera" section
  there.

## 2026-07-09 — combined sweep: fixed all 4 audit findings + 5 dwl.c extractions

- Followed up on the two agent-driven audits from earlier the same day (dwl.c
  modularization progress + whole-codebase bug hunt) with one combined pass:
  fix everything the bug-hunt found, and do all 5 extraction candidates the
  modularization audit named, landing the connection-graph bug fix as part of
  the extraction it naturally belongs to rather than as a separate patch.

### Bugs fixed

- **#1, the real one**: `swap_neighbor_dir()` left off-axis neighbor links
  stale after a swap (full writeup and regression test in
  [[connection-graph]]) — the only one of the four with an actual behavioral
  bug, not just a fragility/inconsistency.
- **#2**: `collect_component()` (now in `connection_graph.c`) silently
  dropped clients past its 256-slot cap with no trace — added a
  `wlr_log(WLR_ERROR, ...)` when the cap is hit, covering all 3 call sites
  (`resolve_growth_overlap`, `close_gap`, `motionnotify`'s drag-component
  collection) from one place.
- **#3**: `toplevel_export.c`'s `client_surface_texture()` dereferenced
  `c->surface.xdg` without the NULL-guard every other pointer in that file
  gets — not reachable today (`surface.xdg` is set once at Client creation,
  never cleared), but it was the one inconsistent spot in a file that's
  already been the site of two real crashes this session (see
  [[quickshell-shell]]). Guarded now, cheap insurance against a future
  refactor that starts invalidating it on unmap the way other fields do.
- **#4**: not a live bug (confirmed: `owned` is only ever set on success in
  every action type today) — documented the ordering invariant
  (`bind_parser.c`, `mode_add_binding()`/`bind_action_parse_arg()`) inline
  instead of adding speculative defensive code for something that can't
  currently happen.

### dwl.c extractions (5 new modules, ~1000 lines out of dwl.c)

`dwl.c`: 5169 -> 4171 lines (working-tree size before this sweep, per the
modularization audit, down to after). New modules, all under
`code/src/modules/layout/` unless noted:

- `connection_graph.c` — `edge_anchor`, `opposite_octant`,
  `octant_from_delta`, `clients_already_linked`, `connect_clients`,
  `collect_component`, `resolve_growth_overlap`, `close_gap`,
  `sever_connection`, `connection_click_hit`, `swap_neighbor_dir` (with the
  bug-1 fix baked in from the start, not bolted on after).
- `directional_focus.c` — `window_center`, `angle_distance_in_cone`,
  `cone_search_focus`, `focus_directional`. Also fixed the pre-existing
  `-Wdeclaration-after-statement`/float-conversion warnings in this code
  while moving it (M_PI cast to float explicitly instead of relying on
  implicit double->float truncation at each use site).
- `client_anim.c` — `spring_step`, `clients_anim_step`,
  `client_set_target_geom` (the spring-glide window animation system).
- `code/src/modules/input/pty.c` — the whole PTY subsystem (spawn's
  pseudo-terminal tracking/logging/inject), previously ~150 lines at the top
  of `dwl.c`.
- `code/src/modules/layout/window_size_history.c` — the app-id/title-keyed
  remembered-window-size lookup table, previously a thin wrapper directly in
  `dwl.c`.

### Cross-cutting fixes needed to make the extractions actually link

- `dwl.c` includes `kalin.h`, but with `DWL_INTERNAL` defined — meaning it
  **does not see** the `#ifndef DWL_INTERNAL` section of `kalin.h` (the
  public-API-for-other-modules declarations), since dwl.c owns those symbols
  and declares them itself. Every function a newly-extracted module needs to
  call *from dwl.c itself* (not just from other modules) needed its own
  forward declaration added directly in `dwl.c`, independent of kalin.h's
  copy — easy to miss, and the actual cause of most of the build errors hit
  during this sweep (`connect_clients`, `resolve_growth_overlap`,
  `sever_connection`, `opposite_octant`, `close_gap`,
  `connection_click_hit`, `collect_component` all needed this).
- `VIEWPORT_ZOOM_SAFE`/`WORLD_TO_SCREEN_X/Y`/`SCREEN_TO_WORLD_X/Y`/
  `SPAWN_GAP` were dwl.c-local macros that `connection_graph.c` also needed —
  moved to `kalin.h`, but specifically **above** the `DWL_INTERNAL` guard
  (the shared-types section), not inside it, so both dwl.c and every module
  see them. Got this wrong once (put them inside the guard first), which
  produced a wall of "undeclared identifier"/implicit-function-declaration
  errors that looked unrelated to each other until traced back to the same
  cause.
- Hit the same "`*/` inside a comment closes it early" mistake as the
  gestures.c incident earlier this session, again — this time in a comment
  I wrote in `dwl.c` referencing `WORLD_TO_SCREEN_*`. Worth actually
  internalizing as a rule: never write a literal `*/` (including as part of
  a wildcard like `FOO_*`) inside a C block comment, full stop.
- `client_anim.c` needs `config.h` (for `anim_stiffness`/`anim_damping`,
  previously dwl.c-only tuning constants) — first TU other than dwl.c to
  include it, which surfaced an "unused variable" warning on `log_level`
  (also in config.h, which this new TU doesn't reference). Marked
  `log_level` `__attribute__((unused))` in both `config.h` and
  `config.def.h` rather than leave the warning or duplicate the file.

### Verification

- `nix develop -c make clean all`: 0 errors on a from-scratch build.
- `make test-unit`: all 5 suites, 0 assertion failures (including the new
  `test_swap_preserves_off_axis_neighbors` regression test).
- Not yet verified live (VM or real hardware) — this was a pure refactor +
  bug-fix pass with no behavior changes beyond the swap-neighbor fix itself,
  which the connection-graph tests now cover directly.

## 2026-07-09 — clicking a minimized window's taskbar icon now unminimizes it

- User request: click a minimized window's icon on the bar to restore it.
  Checked `ftl_request_activate()` (`code/src/modules/foreign_toplevel.c`,
  fired by Quickshell's taskbar `toplevel.activate()` on click) — it only
  ever called `focusclient(c, 1)`, never checked `c->minimized`.
  `setminimized()` disables the client's scene node, so "activating" a
  minimized client this way focused it in name only; the scene node stayed
  disabled and nothing visually reappeared.
- Fixed: if `c->minimized`, call `setminimized(c, 0)` instead of
  `focusclient()` directly (it already calls `focusclient(c, 1)` internally
  on unminimize, so no duplicated call). Build clean, all 36 unit tests
  still pass (no unit coverage for this path — wlroots-dependent). Full note
  in [[foreign-toplevel]]. Not yet verified live.

## 2026-07-09 — found why the crash reproduced on *every* window close, not just with a thumbnail grid open

- The second fix (Loader + pinnedSource snapshot) also recurred live,
  byte-identical trace. Researched whether this is a known upstream
  Quickshell bug first (searched the changelog for v0.2.0/v0.3.0 — the
  version pinned here): no entry matches this specific
  toplevel-closes-during-capture scenario, so it's plausibly still open
  upstream, but that wasn't the actual local cause found.
- **The real finding**: `Overview {}` (`shell.qml`) and `WindowPeek {}`
  (`WindowsBarScreen.qml`) are both instantiated directly at the top level,
  not behind a visibility-gated `Loader`. A Quickshell `PanelWindow`'s
  `visible: false` only hides the panel — the component tree underneath
  stays fully alive. That means every open window's `ScreencopyView` +
  `Timer` inside `Overview.qml` was running `captureFrame()` every
  `thumbnailRefreshMs` continuously in the background *whether or not
  Overview was ever opened*, and `WindowPeek.qml`'s `root.appId` stays set
  to the last-hovered app after the popup closes, keeping that app's
  thumbnails live indefinitely too. This explains why the crash reproduced
  on every window close in general, not specifically while looking at a
  thumbnail — the exposure window was "always," not "only while Overview is
  open."
- **Fixed**: both files' `Loader.active` now also requires
  `OverviewState.visible` / `root.show`, so a `ScreencopyView` only exists
  while the user can actually see it. Combined with the earlier
  pinnedSource freeze (which is still needed — it's a different bug than
  this one), this narrows the close-race window from "the entire lifetime
  of every open window" down to "only while its thumbnail happens to be on
  screen." Full writeup in [[quickshell-shell]].
- `qmllint` clean, no syntax errors. **Still not re-verified live** — two
  prior fixes each looked complete and weren't; don't consider this crash
  class closed until it's actually been exercised on real hardware, closing
  windows both with and without Overview/peek open.

## 2026-07-09 — the null-captureSource crash recurred on real hardware; deeper fix landed

- User rebuilt and relaunched the `kalinwm` dev session on tty3 (real
  hardware, `connector LVDS-1`) with the earlier `Timer.onTriggered` guard
  in place — it **crashed again with the identical trace**
  (`capture_toplevel_with_wlr_toplevel_handle ... null value passed for arg
  2`). The earlier guard only covered `captureFrame()` calls; the crash also
  fires from merely *assigning* a live `ScreencopyView`'s `captureSource`
  property to null (which `captureSource: modelData.toplevel` does the
  instant a window closes) — apparently Quickshell's own compiled
  capture-negotiation code reacts to that property change directly, a path
  no QML timer guard can intercept.
- **Fixed properly this time**: both `Overview.qml` and `WindowPeek.qml` now
  wrap their `ScreencopyView` in a `Loader` keyed on the source's validity,
  and snapshot the capture source once at creation
  (`Component.onCompleted`) into a plain property instead of a live binding.
  Closing a window now destroys the whole `ScreencopyView` instance via the
  `Loader` rather than ever reassigning its `captureSource` to null. Full
  writeup in [[quickshell-shell]], including why the first fix wasn't
  enough.
- Verified with `qmllint` (no syntax errors; only pre-existing-style
  "unqualified access" warnings unrelated to this change, expected since
  qmllint can't resolve Quickshell's own singleton types standalone) — not
  yet re-verified live, since that requires another real-hardware run.
  **Next time this is tested, check whether it actually holds** before
  considering this crash class closed.

## 2026-07-09 — investigated a bar disappearance report; added layer-surface destroy/unmap logging

- User reported the quickshell bar disappearing again, unsure if `Super+Q`
  caused it. Checked `killclient()`/`ACT_CLOSE` (`dwl.c`): `Super+q` only
  ever targets the focused *Client* (xdg-toplevel); the bar is a
  `LayerSurface`, a separate type never in that focus stack — Super+Q
  cannot close it. Ruled out, not just asserted.
- Re-read `/tmp/kalinwm.log`: it still showed the *same* null-captureSource
  crash trace from earlier this session (see the fix above), not fresh
  content — `pgrep` confirmed quickshell (pid 1144) has been running
  continuously since before that fix was made, so **the live tty3 session
  is still the old, unpatched build**; this reported disappearance may well
  be a recurrence of the already-fixed bug, just not yet running the fix.
  Rebuilding/relaunching that session is the way to find out.
- **Gap found and fixed regardless**: `destroylayersurfacenotify()` and
  `unmaplayersurfacenotify()` (`dwl.c`) had zero logging — if a layer
  surface (the bar, wallpaper, etc.) goes away for *any* reason, kalin-wm's
  own log had no trace of it at all, only whatever the client's own stdout
  happened to capture (which, per the note above, is nothing at all under a
  `ly`-started real session). Added `wlr_log(WLR_INFO, ...)` to both,
  logging the surface's `namespace` and `layer`. Full note in
  [[quickshell-shell]].
- Build clean, all 36 unit tests still pass (no unit coverage for this path
  either — wlroots-dependent).

## 2026-07-09 — thumbnail capture reworked to read the client's surface texture directly

- Follow-up to the quickshell crash fix below, prompted by a design question:
  "how do we make window-preview thumbnails work for every client, including
  minimized ones, as performantly as possible." Investigated raw dmabuf
  buffer handoff (literally sharing the client's own committed buffer fd,
  zero GPU work) vs. the existing scene-render approach; raw handoff would
  only work for dmabuf-backed clients (fails "everyone") and loses the
  release-fence lifecycle protocols like wlr-screencopy exist specifically
  to sidestep — rejected in favor of a middle path.
- **Landed**: `code/src/modules/protocols/toplevel_export.c` now captures
  from `wlr_surface_get_texture(c->surface.xdg->surface)` directly instead
  of rendering the client's on-canvas region through a scratch headless
  output + `wlr_scene_output` (the old approach, mirroring `capture.c`'s
  whole-screen screenshot technique). `wlr_surface_get_texture()` normalizes
  both `wl_shm` and `linux-dmabuf` clients into the same `wlr_texture*`, so
  this is backend-agnostic without any special-casing.
- **Why this specifically fixes minimized-window preview**: `setminimized()`
  (`dwl.c`) disables the client's scene node
  (`wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m))`) so a
  minimized window doesn't occupy canvas space — which means the *old*
  scene-render approach would capture nothing for a minimized client. Going
  straight to the surface's texture bypasses the scene graph entirely, so a
  minimized client's last-rendered frame is still there to grab regardless
  of scene-node enable state. Confirmed `client_set_suspended()` (the other
  half of what `setminimized()` does) only sends the xdg-shell suspended
  *hint* to the client — it doesn't invalidate or free anything, so the
  texture stays valid and correct while minimized.
- Destination-side dmabuf/CPU branching is unchanged in spirit but the CPU
  path is new: `wlr_texture_read_pixels()` doesn't scale, and the raw
  surface's native size essentially never matches the destination exactly
  (c->geom includes kalin-wm's border, the surface doesn't) — added
  `read_and_scale_to_cpu_buffer()`, a straightforward CPU nearest-neighbor
  resample, to handle that mismatch (this is the fallback/rare path; the
  common dmabuf-destination path scales for free via the GPU render pass's
  dst_box, same as before).
- Net effect: fewer render passes per capture (one blit instead of
  scene-render-plus-blit), works uniformly across shm/dmabuf clients, and
  the actual feature the user wanted (peek at a minimized window) now works
  at all rather than silently producing a blank thumbnail. Full writeup in
  [[foreign-toplevel]]. Build clean, all 36 unit tests still pass (this
  code path has no unit coverage — wlroots-dependent, needs a real
  compositor); not yet verified live against a real minimized-window
  scenario in the [[test-vm]] or on hardware.

## 2026-07-09 — root-caused and fixed a quickshell crash (null captureSource -> fatal Wayland error)

- User reported quickshell crashing on the real host session (tty3,
  `kalinwm` dev launcher). Initial investigation looked at the wrong layer:
  the `journalctl`/`coredumpctl` trail showed a VT switch (tty3 kalin-wm ->
  tty1 niri) at the same moment, plus a real but apparently non-fatal
  `[ERROR] [backend/drm/drm.c:1385] connector Virtual-1: Failed to disable
  CRTC 37` — confirmed via a from-scratch [[test-vm]] repro (added niri to
  a second VT, see below) that a bare VT switch alone does *not* crash
  kalin-wm. That repro VM build stalled for 35+ minutes on a from-source
  niri rebuild and was killed without ever finishing — abandoned in favor of
  reading actual log output.
- **Actual root cause, found in `/tmp/kalinwm.log`** (the `kalinwm` dev
  launcher's own `tee`'d stdout — nothing quickshell-side reaches
  `journalctl` when the session is started this way, or even via `ly`; see
  the note added to [[quickshell-shell]]): quickshell's own log showed
  `error marshalling arguments for capture_toplevel_with_wlr_toplevel_handle
  (signature 2nio): null value passed for arg 2` immediately followed by
  `The Wayland connection experienced a fatal error: Invalid argument` — a
  QML `Timer` in `Overview.qml`/`WindowPeek.qml` fired a thumbnail
  `captureFrame()` after the window it was capturing had already closed,
  sending a Wayland request with a null (non-nullable) toplevel-handle
  argument. libwayland's client-side marshalling kills the *entire*
  connection on that, not just the one request — unrecoverable and not
  QML-catchable, which is why the whole shell went down rather than just
  that thumbnail erroring.
- **Fixed**: guarded both `onTriggered` handlers to skip `captureFrame()`
  when `captureSource` is null; `WindowPeek.qml` was also missing the
  null-toplevel `visible` guard `Overview.qml` already had. Full writeup in
  [[quickshell-shell]].
- The DRM `Failed to disable CRTC` error is real but separate and still
  unconfirmed as fatal on its own — noted for awareness, not chased further
  this session since the actual crash had a different, now-fixed cause.
- **Lesson for next time a shell/compositor "just crashes"**: check
  `/tmp/kalinwm.log` (dev launcher) first — it's the one channel that
  actually captures quickshell's own stdout/stderr with the real error.
  `journalctl`/`coredumpctl` only see native crashes (SIGSEGV/SIGABRT) with
  a core file, not a clean Wayland-protocol-error exit like this one.

## 2026-07-09 — trackpad gesture navigation + momentum panning implemented

- Built the feature investigated the same day (see the entry below): 3-finger
  swipe pans the [[viewport]] camera, with momentum ("flick") coasting once
  fingers lift, and a pinch zooms. Full behavior/mechanics writeup in the new
  [[gestures]] note; this entry covers what changed and how it was verified.
- New module `code/src/modules/input/gestures.c`, added to `Makefile`'s
  `SRCS`. `gestures_attach()` called from `createpointer()` (`code/src/dwl.c`)
  for every pointer device — a plain mouse just never emits the wlroots
  swipe/pinch events, so the listeners are a harmless no-op for it.
- Uses wlroots 0.20's native `wlr_pointer.events.swipe_begin/update/end` and
  `pinch_begin/update` signals directly — no new Wayland protocol dependency.
- `Viewport` (`code/include/kalin.h`) gained `coasting`, `vel_x`, `vel_y`.
  `viewport_coast_start()` (`code/src/modules/viewport/viewport_ops.c`) starts
  a momentum coast (no-op below `COAST_MIN_START_SPEED`); `viewport_tick()`
  now branches on `viewport.coasting` first, integrating velocity and decaying
  it by `COAST_FRICTION_PER_SEC` each fixed step until `COAST_STOP_SPEED`,
  as a separate physics model from the existing target-lerp easing, sharing
  the same `viewport.animating` flag so `rendermon()`'s frame-scheduling loop
  needed no changes.
- Hit one build error: `gestures.c`'s header comment contained the literal
  text `swipe_*/pinch_*`, whose `*/` closed the C block comment early and
  cascaded into nonsense parse errors (`unknown type name 'pinch_'`, etc.).
  Fixed by rewording to avoid any `*/` substring in the comment body.
- Also had to extend the `Viewport` global's positional initializer in
  `dwl.c` (3 new struct fields, was under-initialized) — caught before it
  became a `-Wmissing-field-initializers` warning.
- **Verification**: `nix develop -c make clean all` clean (only pre-existing
  warnings); `make test-unit` — all 36 tests pass, no regression. Rebuilt and
  booted the [[test-vm]], confirmed no crash/error in the quickshell log, and
  did a manual regression check (spawn + click-focus a terminal) to confirm
  normal mouse/keyboard interaction is unaffected by the new listeners.
  **Swipe/pinch recognition itself was not verified** — QEMU/QMP has no
  synthetic multi-finger gesture input, so this can only be confirmed on the
  user's real trackpad hardware. See [[gestures]] for the explicit caveat.

## 2026-07-09 — code audit; connect_clients() double-link fix; connection-graph + viewport-ops unit tests; driftwm gesture/momentum research

- Ran a broad, non-diff code audit (agent-driven read across `dwl.c` and
  every `code/src/modules/`) plus web research on comparable compositors
  (niri, Hyprland, [[driftwm]] — the second of kalin-wm's two stated design
  inspirations, see [[dwl-fork]]) for feature ideas. Full findings not
  reproduced here; see this entry and the two below for what came of it.
- **Fixed a real correctness bug**: `connect_clients()` (`code/src/dwl.c`)
  only checked whether the *specific slot* `octant_from_delta()` computes
  for the pair's current geometry was occupied — not whether `a` and `b`
  were already linked on some *other* slot (e.g. left over from before one
  of them moved diagonally). A reconnect call whose current geometry lines
  up differently than the stored edge could silently double-link the same
  pair across two octants. New `clients_already_linked()` guard, checked
  first. No behavior change for any of the existing callers' normal cases —
  only blocks the double-link path, which had no test coverage before now.
- **New unit tests** (`make test-unit`, 36 tests total now, up from 21):
  - `code/tests/test_connection_graph.c` (8 tests) — `connect_clients()`
    (basic link, slot-occupied no-op, the new double-link regression case),
    a pointer-keyed stand-in for `sever_connection()`, `close_gap()`
    (downstream-component shift, no-op when unconnected), and
    `swap_neighbor_dir()`'s three-way connection-transfer bookkeeping
    (the "middle of a 3-chain" case its own code comment describes).
  - `code/tests/test_viewport_ops.c` (7 tests) — `viewport_ensure_visible()`
    (no-op when already visible, edge-alignment not centering, oversized-
    window near-edge alignment — regression coverage for the "jumpy camera"
    fix) and `viewport_menu_reveal()`'s pan-amount math (no-op with room,
    correct pan near an edge, skip above the dock-mode width threshold).
  - Both follow the existing `test_growth_overlap.c` pattern: standalone
    reimplementations of the real logic against minimal Client/Monitor
    stand-ins, since `dwl.c`/`viewport_ops.c` can't link without a running
    wlroots backend. Wired into `Makefile`'s `test-unit` and `clean` targets.
  - This was flagged as the single biggest test-coverage-to-complexity
    mismatch in the repo — the connection-graph and viewport code are both
    newer and among the most actively-changed this week, with the least
    coverage.
- Verified in the [[test-vm]]: spawned two terminals and confirmed the
  connection graph still forms normally under the new guard (no regression
  in the common case).
- **Investigated (not implemented) two further items** — see
  [[connection-graph]] and the roadmap for where these land:
  1. **Trackpad gesture navigation + momentum panning**, driftwm-inspired.
     kalin-wm currently has *zero* touchpad gesture support — confirmed via
     grep, no code anywhere listens for swipe/pinch. wlroots 0.20 (the
     version this project already links) exposes `wlr_pointer.events.
     swipe_begin/update/end` and `pinch_begin/update/end/rotation/scale`
     directly on the same `wlr_pointer` a touchpad already is — no new
     protocol dependency needed, just new listeners attached in
     `createpointer()` (`code/src/dwl.c`, where touchpad libinput config
     already happens). Concrete design sketch for later: 3-finger swipe ->
     pan (reusing `viewport_pan_grab_update()`'s existing screen-delta math),
     pinch -> zoom, and a new velocity-decay "coast" mode for momentum
     (distinct from `viewport_tick()`'s existing target-lerp easing) fed by
     the last few swipe deltas before `swipe_end`. Auto-pan-when-dragging-
     past-the-viewport-edge (also a driftwm feature) would extend
     `motionnotify()`'s `CurMove` branch similarly. None of this is
     implemented yet — this is a design starting point, not a spec.
  2. **The 5 stale TODO/FIXME/HACK comments** (`code/src/dwl.c`, found by the
     audit): re-read each in context.
     - `:2697` `/* TODO do we actually require a cursor? */` — effectively
       answered "yes" by everything built since (spawn-under-cursor,
       pan-grab, connection-line hit-testing all assume a cursor
       pervasively); safe to delete outright.
     - `:2690` `/* TODO handle other input device types */` (switch default
       case for non-keyboard/pointer devices) — likely a deliberate non-goal
       for a personal single-user laptop compositor (no touch/tablet
       hardware to test against), worth rewording as an explicit non-goal
       rather than an open TODO, not treating as a real gap.
     - `:1440` (cursor icon not restored to under-cursor client's preferred
       shape after a move/resize/pan ends) and `:5011` (cursor jumps to 0,0
       after all monitors wake from DPMS) are both genuine, still-current,
       low-priority cosmetic bugs — worth a `roadmap.md` line each rather
       than staying as easy-to-miss inline comments.
     - `:4202` (cursor's very-first on-screen position at startup) is a
       minor, low-priority startup cosmetic nit, same call.
     - None of these were fixed or moved this session — investigation only.

## 2026-07-09 — spawn under cursor when unfocused; pan camera to reveal the hold-Super arc menu near a screen edge

- Requested: (1) a new window with no spawn-parent (nothing focused, or
  focus is on another monitor) should appear under the cursor instead of at
  the monitor's geometric center; (2) opening the hold-Super menu
  ([[window-menu]]) on a window that isn't full-width but sits close to the
  right screen edge should pan the camera right a bit so the arc isn't cut
  off.
- **(1)** New branch in `mapnotify()`'s placement fallback (`code/src/dwl.c`)
  between the spawn-parent path and the old monitor-center fallback: when
  there's no usable spawn-parent, checks `xytomon(cursor->x, cursor->y) ==
  c->mon` and if so centers the new window on `SCREEN_TO_WORLD_X/Y(cursor->x/
  y)` instead. Monitor-center is now only reached when the cursor is on some
  *other* monitor than the new window. See [[spawn]].
- **(2)** New `viewport_menu_reveal(Client *c)`
  (`code/src/modules/viewport/viewport_ops.c`), called from the
  `ACT_WINDOW_MENU` bind case right when `menu_shown` flips to 1: no-op
  above the shell's own 85%-width dock threshold (see [[window-menu]] — a
  docked menu doesn't care where the window sits), otherwise pans the
  camera right by however much of a fixed ~300px reserve past the window's
  right screen edge is currently missing. Reuses `viewport_pan()` (the
  existing IPC `pan` command's underlying function) rather than poking
  `viewport.target_x` directly, so it's a normal animated pan, not a snap.
- Verified in the [[test-vm]]: closed the initial terminal, moved the cursor
  to an empty area, and `Super+T` landed the new terminal there rather than
  at monitor-center; then panned camera so a (non-full-width) terminal sat
  with only ~90px clear to its right and confirmed holding Super slid the
  camera right until all five menu buttons were fully on screen.
- Build clean, 21/21 unit tests green.

## 2026-07-09 — camera feels jumpy: scroll-into-view instead of centering; lock focus while the hold-Super menu is open

- Reported: the camera "feels too jumpy" — [[follow-mode]] recentered the
  focused window dead-center on every focus change instead of just keeping
  it in view, and switching focus while the hold-Super menu was open
  ([[window-menu]]) let it move to a different window, which repositioned
  the menu and re-panned the camera at the same time.
- **`viewport_ensure_visible()`** (new, `code/src/modules/viewport/
  viewport_ops.c`), used only by `viewport_follow_focus()`: no-op if the
  focused window is already fully on screen; otherwise pans the camera the
  minimum distance to bring the nearer out-of-view edge into frame (aligns
  to that edge on an axis where the window is bigger than the viewport,
  rather than trying to fit both edges). `viewport_center_on()` itself is
  unchanged and still used for the deliberate "jump to this window" cases
  (taskbar fly-to-window, auto-pan to a newly spawned window) — this was a
  passive-follow-only fix, not a change to those.
- **Focus-lock while the menu is up**: `focusstack()` and
  `focus_directional()` (`code/src/dwl.c`) both early-return while
  `menu_shown` is true, so `Super+J`/`K`/`Arrow` no longer switch focus (and
  therefore can't move the menu or the camera) until the menu closes.
  `swap-dir` and click-to-focus are deliberately left alone — only the two
  focus-*switching* binds were in scope.
- Verified in the [[test-vm]] with two real windows of different sizes (a
  small `foot` terminal and a full-width `firefox`): panning the camera away
  and cycling focus back with `Super+J` now slides the terminal in flush to
  the screen edge with Firefox still mostly visible, instead of snapping the
  terminal to dead center; holding Super over Firefox (docked menu) and
  pressing `J` left focus and the menu both on Firefox. Also re-confirmed,
  the hard way again, that **the ad hoc "windows" visible in most of this
  session's earlier screenhots are wallpaper decoration**
  (`wallpaper_build_blue_room_tile()`'s "framed window" rects, repeating
  every wallpaper tile), not real clients — cost time until re-realized
  mid-test that clicking/cycling onto them predictably did nothing. Worth
  remembering for future VM screenshots: only trust a window as real if it
  has visible content or was deliberately spawned, not just because it looks
  like a bordered rectangle.
- Build clean, 21/21 unit tests green.

## 2026-07-09 — hold-Super menu revamp: dock mode, on/off states, overlap toggle, warm retheme

- Requested: fix the hold-Super menu not working for full-width windows, warm
  up the color scheme (oranges/yellows/browns) across the whole rice, show
  on/off state on toggle buttons (explicitly: fullscreen), remove the dead
  floating action, add an "allow overlap" button, and make the
  [[connection-graph]] lines easier to click.
- **Dock mode.** `WindowActions.qml`'s arc flies out from the focused
  window's right edge — for a window at/near full screen width there's no
  room to its right, so past a width threshold (≥85% of screen width,
  `radial.dockMode`) the menu switches to a straight vertical dock pinned to
  the screen's right edge instead, sliding in from off-screen rather than
  bowing out from the window edge. See [[window-menu]] for the full current
  design (rewritten — the old note predated this and several earlier
  changes and had drifted).
- **On/off states.** Each action entry now carries a `state` (null for a
  momentary action, bool for a toggle); a toggle button gets a filled/bright
  treatment plus a small indicator dot when on. Applied to Fullscreen, Crop,
  and the new Overlap button.
- **Removed the dead Tile/Float action.** It read
  `KalinViewport.focusedFloating`, a field the compositor stopped sending
  once the tiled/floating split was removed (see [[column-layout]]) — so
  it had been silently inert (always "Float", no-op) with nobody noticing.
  Also renamed the old "Move"/`Ctrl←→` entry to "Swap": it still described a
  `move-column` action that no longer exists; the real current bind on that
  chord is `swap-dir`.
- **New "allow overlap" feature**, compositor-side, not just a shell toggle:
  new `Client.allow_overlap` flag, `toggle-overlap` bind action
  (`Super+Shift+O`), `toggleoverlap()` (`code/src/dwl.c`, mirrors
  `toggleontop()`). `resolve_growth_overlap()` now returns immediately for a
  client with the flag set, so it's exempt from ever pushing connection-graph
  neighbors aside. IPC gained `"focused":{"overlap":bool}`. See
  [[connection-graph]] for the mechanism it opts out of.
- **Connection-line clickability**: bumped the compositor's
  `CONN_HIT_RADIUS_PX` 12→20 (`dwl.c`) and tightened `LineGeometry.qml`'s
  dot spacing 20→14px plus slightly larger glyphs, so the visible dotted
  line reads closer to the actual clickable width instead of implying a
  much thinner target.
- **Warm retheme** (oranges/yellows/browns, replacing cyan/gray):
  `Theme.qml` (the shell-wide color singleton — most widgets pick this up
  automatically) and the compositor's own procedurally-drawn "blue room"
  wallpaper tile (`code/src/modules/ui/wallpaper.c`, recolored to a warm
  amber/ochre "study" palette, same geometry). Also caught one
  Theme-bypassing hardcoded color on the taskbar's icon-fallback badge
  (`TaskbarButton.qml`, was `"#2f4a7a"`, now `Theme.accentBlue`). **Not**
  retinted this pass, as a scope cut: the deeper settings panels (Calendar,
  Mixer, Display, System) have many more one-off hardcoded hex colors not
  routed through `Theme` — a real follow-up if the warm retheme should cover
  those too.
- **Testing gotcha re-hit, already documented and still true**: the test
  VM's disk persists `~/.config/kalin-wm/binds.conf` across `vmctl down`/`up`
  (only the QEMU process restarts, not the disk) — see [[test-vm]]. Cost
  real time re-discovering this despite it already being written down,
  because the vault wasn't checked before testing a brand-new keybind.
  Confirmed by deleting the VM's `.qcow2` outright (blunter than the
  documented fix of deleting just `binds.conf` inside the guest, but
  equally effective) and re-testing on a clean boot — the "Overlap" toggle
  and its on-state indicator both confirmed working end-to-end only after
  that.
- Verified in the [[test-vm]] with two different apps as instructed (a
  small `foot` terminal and a full-width `firefox` window) confirming both
  the arc and dock layouts, the on/off indicators, and the overlap toggle.
  Build clean, 21/21 unit tests green.

## 2026-07-09 — quickshell bar crash investigation: buffer-negotiation churn, not raw memory volume

- Reported: "the quickshell bar is still crashing" + asked to research a
  permanent fix for "the memory problem" and update this vault to current
  scope (this pass covers that vault update too — see the entries below and
  throughout, especially [[connection-graph]] replacing
  [[column-layout]]/[[anchored-window]], which had drifted undocumented for
  a while).
- Investigated with `coredumpctl` (no `gdb` installed by default — used
  `nix-shell -p gdb`) against real crash coredumps and
  `/run/user/1000/quickshell/by-id/*/log.log` (30+ accumulated instance
  dirs — Quickshell's own `checkCrashRelaunch` silently relaunches on
  crash, so this reads to the user as "the bar glitches," not an obvious
  crash). Full findings in [[stability]]; summary: the notification-popup
  QML crash (`VDMListDelegateDataType::createMissingProperties`) was already
  fixed (`NotificationService.qml`'s `ListModel`, 2026-07-06, confirmed no
  recurrence since); the *actual* still-ongoing driver was `Overview.qml`/
  `WindowPeek.qml` running every visible thumbnail's `ScreencopyView` in
  continuous `live: true` mode, which on this hardware's dmabuf-format
  mismatch meant every tile, every compositor frame, failed dmabuf
  negotiation and fell back to SHM — forever, for as long as a preview
  overlay stayed open. Fixed by throttling both to a periodic
  `captureFrame()` via a new `OverviewState.thumbnailRefreshMs` (2000ms).
- Framing note for future work: "the memory problem" was better described
  as an unbounded *rate* of failed allocation attempts (scaling with open
  windows × frame rate) than a leak of raw bytes — the fix is a throttle,
  not a leak plug. The underlying dmabuf-format mismatch itself is still
  unresolved; re-check `coredumpctl list | grep quickshell` for a new
  signature if crashes recur.

## 2026-07-08/09 — connection-graph replaces column-layout/anchored-window; persistence rework; assorted crash + correctness fixes

- **Architecture pivot** (had already happened by the start of this working
  session, undocumented until now — see [[connection-graph]] for the full
  current model): the old dual tiled/floating [[column-layout]] +
  [[anchored-window]] split is gone. Every window is free-positioned at a
  persistent world position; windows connect to spawn-adjacent neighbors via
  an 8-octant graph (`Client.neighbor[8]`) that drives group-drag,
  directional swap (`Super+Ctrl+Arrow`), spawn placement/insertion, and
  overlap avoidance on growth. As part of the same pivot, `Super+F`/
  `Super+Shift+F` were rebound from the 2026-07-07 entry's
  `toggle-maximized` to `fit-width`/`fit-height` (`fitwidth()`/
  `fitheight()` below) — `toggle-maximized`'s floating-detour mechanics
  don't make sense once there's no floating state to detour through.
- **Overview bug: window overlapping when it grows after being placed.**
  Reported as "spawn a window over-max-width, `Super+F` bugs out and breaks
  the tiling." Two independent causes found and fixed:
  1. `fitwidth()`/`fitheight()` (`Super+F`/`Super+Shift+F`) were resetting
     the window's world `x`/`y` to the monitor's placement anchor
     (`mon->w.x/y`) instead of leaving position alone — teleporting it away
     from its actual connection-graph neighbors and dropping it on whatever
     else lived near that anchor. Fixed to grow in place.
  2. Neither action pushed connection-graph neighbors out of the way when
     growing, unlike a client's own post-map growth (`resolve_growth_
     overlap()`, wired into `commitnotify()`). Now calls it explicitly.
  Verified end-to-end in the [[test-vm]] and with a standalone unit test
  (`code/tests/test_growth_overlap.c`, added since the real logic can't link
  standalone — dwl.c needs a running wlroots backend).
- **The original overlap bug this session started from:** spawning a big
  window (Obsidian was the repro target; substituted Firefox in the VM,
  since VM lacks Obsidian) over an existing chain overlapped a neighbor,
  because nothing re-ran collision avoidance when a client's size settled
  *after* spawn-placement (common for Electron/GTK apps — small placeholder
  size at map, real size on a later commit). Fixed the same way as (2)
  above: `resolve_growth_overlap()`, called from `commitnotify()` whenever
  `client_accept_requested_size()` reports growth.
- **Persistence reworked** — see [[persistence]] for full detail:
  - Multi-instance identity keying: same-appid+title windows (two terminals)
    were colliding onto one saved slot; now keyed by spawn-order `instance`,
    with `identity_key()` = appid-primary (title alone proved unsafe to key
    on — it can change between a client's default and its real title
    depending on exact scheduling, which was observed splitting one
    counter into two and silently failing every match).
  - The [[connection-graph]] itself is now saved/restored too, not just
    position/size.
  - **Root cause of persistence silently never working at all:**
    `persistence_init()`'s `mkdir()` was non-recursive and
    `~/.local/share` doesn't exist by default on a minimal system (the
    [[test-vm]] never had it) — every `fopen()` after that failed silently.
    Fixed with a small `mkdir -p` helper. The identical bug, found the same
    way (investigating a real segfault with no crash log to show for it),
    was also in `crash_report.c`'s own log-directory creation.
  - A restored width/height for a **brand-new** client could be silently
    clobbered by that client's own first size-negotiation commit arriving
    after the restore — fixed with a one-shot `Client.persist_size_pending`
    flag consumed in `commitnotify()`.
  - Verified with a full VM power-cycle: 3 windows (one stretched via
    `fitwidth()`) restored exact position, size, and connection topology.
- **Connection-graph gap-closing on window removal.** Requested: closing the
  middle of a connected line left a visual gap even after the two remaining
  neighbors got spliced together. Added `close_gap()`
  (`code/src/dwl.c`), called right after the splice in `unmapnotify()`:
  shifts the whole downstream component on one side so the facing
  edge-to-edge distance becomes exactly `SPAWN_GAP` again.
  - Also fixed the splice itself while implementing this: `connect_clients()`
    silently no-ops if either side's target slot is already occupied, and at
    splice time the two neighbors' slots still pointed back at the closing
    window (not yet cleared) — always refused to link. Fixed by clearing
    just those two back-pointers first. Found via temporary `wlr_log()`
    tracing in the VM, since the failure was silent (no crash, just no
    edge — a real window count sanity-check in the VM first suggested
    windows were vanishing, which turned out to be a red herring: `pgrep`
    confirmed both survivors were alive, just positioned far apart with no
    line between them).
- **Real-hardware segfault in `toplevel_export.c`** (buffer-size-mismatch
  heap overflow in the CPU-readback path) — see [[stability]] for detail.
  Exposed by `fitwidth()` actually working correctly for the first time.
- Build clean, unit tests green throughout (`make test-unit`, 21/21 — up
  from 18 with the new growth-overlap test file). Every fix VM-verified via
  `test-vm/scripts/vmctl.py`, several also confirmed via direct IPC state
  probes (`connections`/`viewport`/`rect` JSON), not just screenshots.
- Hit the host disk-full crisis (`nix-collect-garbage -d`, ~30GB freed) twice
  more this session from repeated VM rebuild cycles — same recurring cost
  noted in earlier ledger entries, still unresolved as a workflow problem.

## 2026-07-07 — feature: `Super+F` toggle-maximized (fill work area, not fullscreen)

- Request: widen the focused window to fill the screen without truly
  fullscreening it (border, focus ring, and the bar should stay visible).
- Replaced `Super+f`'s previous binding (`layout floating`, a direct-select
  shortcut redundant with `Super+space`'s toggle) with `toggle-maximized`,
  per explicit user choice among three options (the alternatives were
  `Super+Shift+F` or an unused key like `Super+m`).
- New `ACT_TOGGLE_MAXIMIZED` action; `setmaximized()`/`togglemaximized()`
  in `code/src/dwl.c`, new `Client` fields `ismaximized`, `premax` (geometry),
  `premax_floating`, `premax_world`.
- Mechanically: forces the client floating and resizes it to
  `c->mon->w` (the monitor's usable work area, excluding layer-shell
  reserved space like the bar — as opposed to `setfullscreen()`'s `c->mon->m`,
  the *whole* monitor). Unlike fullscreen, `c->bw` stays the real border width
  and it stays in `LyrFloat`, not `LyrFS`.
- Hit a real bug during VM testing: a maximized window initially rendered
  shifted off-screen. Root cause — `c->mon->w` is in output-layout (screen)
  space, but for a normal floating/tiled client (`LyrFloat`/`LyrTile`),
  `c->geom` is *world* space, transformed to screen space every frame via
  `WORLD_TO_SCREEN` in `client_apply_zoom_frame()`. Setting `c->geom` directly
  to a screen-space rect then re-ran it through that transform a second time,
  landing it wherever the camera's current pan/zoom happened to put it.
  Fullscreen avoids this by special-casing `c->isfullscreen` in both
  `client_apply_zoom_frame()` and `client_set_buffer_scale()` to skip the
  camera transform entirely — added `c->ismaximized` to those same two
  bypasses, since "fill the monitor's work area" should mean the *physical*
  screen area regardless of camera state, same as fullscreen.
- Second bug, same test pass: un-maximizing a previously-tiled window didn't
  restore its original position — it re-tiled into a fresh column instead.
  Cause: the restore path calls `setfloating(c, 0)`, which unconditionally
  clears `c->world.set` on a floating→tiled transition (intentional there:
  a window the *user* explicitly un-floats should flow into a fresh column,
  Niri-style, not snap back). But the floating detour during maximize is an
  implementation detail, not a real user float — fixed by snapshotting
  `c->world` before maximizing and restoring it after `setfloating(c, 0)`
  clears it, so the client lands back in its exact original column/position.
- Confirmed in the VM: maximized state fills the work area edge-to-edge with
  border and bar visible; toggling back restores the exact original x/y
  (height re-stretching to fill the column on restore is a pre-existing,
  unrelated layout quirk — reproduced identically via the ordinary
  `Super+Shift+Space` floating toggle, nothing to do with this feature).
- Updated `default_binds.h`, `config.h`/`config.def.h` (compiled fallback
  `keys[]`), `bind_actions.c`, and [[keybindings]]. Build clean, 22/22 unit
  tests green.

## 2026-07-07 — feature: implement hyprland-toplevel-export-v1 (window previews) — confirmed working on VM and real hardware

- Reported: taskbar hover-preview thumbnails (`WindowPeek.qml`) and the
  [[overview-mode]] grid's thumbnails render blank on kalin-wm.
- Root-caused by disassembling Quickshell's compiled `ScreencopyManager::
  createContext` dispatcher (0.3.0): for a `Toplevel` capture source it is
  **hard-locked to `hyprland-toplevel-export-v1`**, a Hyprland-proprietary
  protocol, with no fallback to the standard `ext-image-copy-capture-v1`/
  `ext-foreign-toplevel-image-capture-source-v1` pair (that pair's client
  code exists in the binary but is dead code for the Toplevel case — only
  ever used for whole-output capture). Confirmed via binary inspection, not
  docs, since the docs' own claim ("needs hyprland-toplevel-export-v1")
  turned out accurate but a general web search suggesting the standard pair
  as an alternative was not.
- wlroots has no wrapper for this protocol (not wlroots-blessed) — hand-
  implemented the whole server side, new module
  `code/src/modules/protocols/toplevel_export.c`:
  - Vendored `protocols/hyprland-toplevel-export-v1.xml` (found already
    present on this machine, in Quickshell's own build sources and a
    Hyprland dev package) and, for the first time in this codebase, needed
    `wayland-scanner private-code` (not just `server-header`) since nothing
    else already links in this protocol's `wl_interface`/`wl_message` data
    tables — new Makefile rule `code/include/protocols/%-protocol-code.c:
    protocols/%.xml`.
  - Its `capture_toplevel_with_wlr_toplevel_handle` request types its handle
    arg as `zwlr_foreign_toplevel_handle_v1` (the *old* wlr-foreign-toplevel-
    management protocol kalin-wm already implements internally via wlroots —
    confirmed keys off the exact same handle type `foreign_toplevel.c`
    already creates per-`Client`, so no new toplevel-listing protocol was
    needed). But generating code that merely *references* that foreign
    interface by name needs its `wl_interface` symbol linkable too, and
    wlroots doesn't export it (it's used privately inside wlroots' own
    implementation) — so `wlr-foreign-toplevel-management-unstable-v1.xml`
    also had to be vendored+`private-code`-generated, purely to provide that
    one symbol; kalin-wm's actual foreign-toplevel protocol implementation
    stays entirely wlroots-internal (`wlr_foreign_toplevel_manager_v1_create()`),
    this generated copy is never registered as its own global.
  - Rendering reuses `capture.c`'s exact scene-render-to-pixels technique
    (throwaway headless `wlr_output` + `wlr_scene_output` positioned over a
    region of the shared scene, `wlr_texture_read_pixels()`) — scoped to one
    window's region instead of the whole monitor, kept as a persistent
    shared scratch rig (not recreated per capture) since Quickshell requests
    frames repeatedly while a preview is live-hovering.
- VM-tested with temporary `wlr_log` probes at every step (removed before
  done, per this session's standing discipline): confirmed the protocol
  dispatch chain works fully — Quickshell successfully binds the manager,
  calls `capture_toplevel_with_wlr_toplevel_handle`, the handle correctly
  resolves back to the right `Client*`, the `buffer`/`buffer_done` events
  send correctly.
- The pixel-copy step took three more rounds to get right, each producing a
  different, more specific error than the last:
  1. First cut assumed the client buffer would be `wl_shm`-backed (Quickshell's
     own log claims "DMA buffer creation failed, falling back to SHM") —
     `wl_shm_buffer_get()` on the buffer resource failed regardless.
  2. Switched to `wlr_buffer_try_from_resource()` (generic import) +
     `wlr_renderer_begin_buffer_pass()` GPU blit, assuming dmabuf-backed since
     it wasn't shm — import succeeded but the render pass itself failed:
     wlroots' GL renderer can't target a plain CPU-mapped buffer with a render
     pass.
  3. Branched explicitly on `wlr_buffer_get_dmabuf()` (GPU render-pass path)
     vs `wl_shm_buffer_get()` (CPU path) — the buffer matched *neither* check,
     despite importing fine via `wlr_buffer_try_from_resource()`: some other
     wlr_buffer-backed type, not plain shm nor dmabuf as wlroots' specific
     type-checks recognize them.
  4. Fix: kept the dmabuf render-pass branch, but replaced the shm-specific
     `wl_shm_buffer_get()` fallback with the generic
     `wlr_buffer_begin_data_ptr_access()`/`_end_data_ptr_access()` API, which
     works for *any* CPU-accessible `wlr_buffer` regardless of which protocol
     produced it, not just `wl_shm_pool`-backed ones. This was the one that
     worked.
- Initially guessed the failure was a VM-only `virtio-vga-gl` (virgl) dmabuf
  quirk and that real hardware would sidestep it entirely — **wrong**: the
  user tested on real hardware (`/tmp/kalinwm.log`) and hit the exact same
  failure, disproving the hypothesis and requiring fix attempts 2–4 above.
  Lesson: don't treat an environment-specific theory as resolved without
  actually testing it.
- Confirmed working after fix 4: rebuilt, rebooted the VM, spawned a
  terminal, hovered its taskbar icon — the `WindowPeek.qml` popup now shows
  live thumbnails matching real on-screen window content (previously blank),
  with zero `toplevel_export:` errors and zero "screencopy failure" warnings
  in the logs. [[overview-mode]]'s grid thumbnails share the same capture
  path and benefit the same way.
- Added `move X Y` (pure pointer move, no click) to
  `~/environment/test-vm/scripts/vmctl.py`, alongside the existing `click` —
  needed to trigger the taskbar's hover-preview at all (a click activates
  the window instead of hovering it).
- Build clean, 22/22 unit tests green.

## 2026-07-07 — feature: clicking a window in [[overview-mode]] jumps to it (not restore)

- Changed the click behavior: clicking a window while `Super+O` is open now
  centers the camera on *that* window at 1.0 zoom (`overview_select()`,
  `code/src/modules/viewport/overview.c`), instead of restoring the
  pre-`Super+O` camera (which is now only what `Super+O` toggle-close or a
  bare `Escape` — dismissing *without* clicking a window — still do, via the
  unchanged `overview_exit()`).
- Deliberately doesn't call the existing `viewport_center_on()`: that
  function centers using the *current* (`viewport.zoom`) value, which at the
  moment of the click is still the overview's zoomed-out level — using it
  here would compute the pan target for the wrong zoom and land off-center
  once the camera finished animating to 1.0. `overview_select()` instead
  computes the centering offset directly against the *target* zoom (1.0),
  mirroring the same target-first pattern already used by
  `viewport_fit_all()`/`viewport_reset()`.
- Build clean, 40/40 unit tests green. VM-verified: zoomed to 81%, opened
  the overview (zoomed further out to fit 4 windows), clicked a window that
  was neither the pre-overview-focused one nor centered on screen in the
  overview grid — camera landed centered on exactly that window at 100%
  zoom, focused (cyan border).

## 2026-07-07 — fix: two more real bugs found testing Overview (border lag, fullscreen scaled by camera zoom)

- **Border/focus-ring lag during a smooth camera zoom.** User reported (real
  hardware, right after the [[overview-mode]] ship) that `Super+O`'s zoom-out
  visibly showed the border/focus-ring outline lagging behind the shrinking
  content. Root cause: border and focus-ring rect sizes were only ever
  recomputed inside `resize()` (spawn, explicit resize, fullscreen toggle,
  animation settle) — `viewport_camera_tick()` (the lightweight per-frame
  refresh used during any smooth camera pan/zoom) only repositioned each
  window's frame origin, never resized the border/focus-ring rects to match
  the new zoom. Content tracked the zoom fine (`client_set_buffer_scale()`
  already re-applies every frame in `rendermon()`), so the border box visibly
  detached from the (correctly scaled) content — a pre-existing gap in any
  smooth zoom, just far more visible on Overview's big, sudden jump than on
  incremental `Super+Ctrl+=/-` presses.
  Fix: extracted the border/focus-ring sizing out of `resize()` into a shared
  `client_apply_zoom_frame()` (`code/src/dwl.c`), called from both `resize()`
  and every frame from `viewport_camera_tick()`, so the frame now tracks the
  camera in lockstep with the content during any pan/zoom, not just at
  settle.
- **Fullscreen scaled/shifted by whatever the camera happened to be doing.**
  Same real-hardware test, next thing tried: fullscreen while zoomed out
  didn't fill the screen. Root cause (this one pre-dates this session, first
  spotted and only diagnosed earlier in the cycle, never fixed until now):
  `setfullscreen()` sets `c->geom` to `c->mon->m` (the monitor's rect in
  shared output-layout space) — correct — but `resize()`'s frame positioning
  and `client_set_buffer_scale()`'s content scaling both then ran that rect
  through the ordinary `WORLD_TO_SCREEN` camera transform (pan + zoom) meant
  for canvas content, shifting/shrinking it away from where the monitor
  actually sits in layout space instead of leaving it there untransformed.
  Fullscreen must always fill the physical monitor 1:1, independent of the
  camera. Fixed by special-casing `c->isfullscreen` in both
  `client_apply_zoom_frame()` (identity position, zf=1.0) and
  `client_set_buffer_scale()` (scale forced to 1.0) — fullscreen content and
  frame are now camera-independent entirely.
- Build clean, 40/40 unit tests green. VM-verified: zoomed the camera to 73%,
  toggled fullscreen — window filled the full 1280x800 screen edge to edge;
  toggled back — returned to the tiled layout at the same 73% zoom, unchanged.

## 2026-07-07 — feature: native niri-style [[overview-mode]] on Super+O

- Replaced the shell-delegated `Super+o -> spawn qs ipc call windows-bar
  toggleOverview` with a native compositor action, `toggle-overview`
  (`ACT_TOGGLE_OVERVIEW`), implemented in a new module
  `code/src/modules/viewport/overview.c`.
- Researched niri's real Overview (v25.05, `Mod+O` default — same key kalin-wm already
  used): zooms the camera out to the actual spatial layout, live, click-to-jump, all
  keybinds still work while open. Recognized kalin-wm doesn't need a separate
  renderer for this: it already has a real camera over a real 2D canvas, and
  [[fit-all]] (`viewport_fit_all()`, `Super+0`) already computes exactly the "zoom out
  to frame everything" shot niri's Overview needs. So "Overview" here is `fit_all`
  promoted to a toggle that saves the camera on entry and restores it on exit (second
  `Super+O`, clicking a window, or a bare `Escape`) — see [[overview-mode]] for the
  behavior and the now-decided [[research/active-design/overview-mode|research note]]
  (was "deferred," now shipped).
- Wiring follows this session's modularization rule: `dwl.c` only gained the one bind-
  dispatch case, one `buttonpress()` line (exit-on-click), and a bare-`Escape` intercept
  in `modules/input/keyboard.c` mirroring the existing crop-mode `r`-intercept pattern
  exactly. All real logic lives in the new module.
- Two real bugs surfaced during VM verification, both fixed:
  1. **New files invisible to the VM's Nix build.** `overview.c` (and, discovered
     retroactively, `arrange_sched.c` from earlier this session) were never
     `git add`-ed. The flake's `path:` input only sees the git-tracked view of the
     tree, so a wholly new file silently drops out of the build — `nix build .#vm`
     either fails with a confusing `No rule to make target .../overview.o` or, worse,
     could silently link a stale binary with no error if a prior `.o` happened to
     already exist. Fixed by staging (`git add`, not committing) the new files. Noted
     as a sharp gotcha in [[test-vm]].
  2. **Stale persisted `binds.conf` on the VM's disk.** `binds_init()` only writes the
     compiled `DEFAULT_BINDS` to `~/.config/kalin-wm/binds.conf` if that file doesn't
     already exist — the VM's disk survives `vmctl down`/`up`, so a `binds.conf`
     written on an *earlier* boot (before this session's rebind) kept loading the old
     `Super+o` spawn command indefinitely. Diagnosed by realizing Super+O produced no
     visible effect despite the code being correct and present in the binary; fixed by
     deleting the file from a terminal inside the VM and rebooting. Also noted in
     [[test-vm]] (an edit alone, no reboot, would also have worked — `binds.conf`
     hot-reloads live via inotify).
- Added a `click X Y [BUTTON]` command to `~/environment/test-vm/scripts/vmctl.py`
  (QMP `input-send-event`, mouse move+click) — no mouse input existed before, needed to
  test click-to-focus-and-exit. Verified precisely with distinctly-labeled terminals
  (typed but not executed `echo ONE`/`echo TWO`/`echo THREE` into each) to remove any
  ambiguity about which window a click actually focused.
- VM-verified end to end: `Super+O` zooms out to the fit-all shot; clicking a specific
  labeled window focuses exactly that window and restores the camera to the exact
  pre-`Super+O` position/zoom; toggling `Super+O` again restores it the same way;
  bare `Escape` restores it the same way; bare `Escape` when the overview isn't open is
  a no-op (doesn't eat input meant for the focused client). Build clean, 40/40 unit
  tests green.

## 2026-07-07 — fix: real hardware freeze on window spawn (rendermon full-output skip)

- Reported: window spawn still froze the whole screen noticeably on real
  hardware, then "teleported," despite the arrange/printstatus coalescing work
  earlier this cycle. The VM never showed it as badly.
- Two real, separate causes found and fixed:
  1. **`seat` group missing.** The user account wasn't in the `seat` Unix
     group, so `libseat` couldn't connect (`Permission denied`) and wlroots
     fell back to a degraded input path — matched a `libinput ... lagging
     behind, your system is too slow` warning. Fixed via `home-config`
     (`users.nix` `extraGroups` +`"seat"`) + `nixos-rebuild boot` + reboot
     (this system's nixpkgs pin had also drifted onto the dbus-broker
     default, which blocks a live `switch` — `boot` sidesteps that check).
     Confirmed: the libinput warning disappeared afterward, but the freeze
     itself didn't — this wasn't the whole story.
  2. **The real root cause, in `rendermon()` (`code/src/dwl.c`):** it skipped
     `wlr_scene_output_commit()` for the *entire monitor* whenever any tiled,
     visible client had an outstanding (un-acked) resize serial. A brand-new
     window gets exactly that the moment `arrange_columns()` assigns it a
     size — and the ack only lands once the new process has started,
     connected, and committed a matching buffer, which on a cold real-GPU
     process start can take a very visible amount of time. During that
     window *nothing* on the monitor rendered — not other windows, not the
     cursor — then every backed-up scene change landed in one commit once
     the slow client caught up (the "teleport"). This check never actually
     protected interactive mouse-resize either, since `moveresize()` always
     floats the grabbed client first (excluded by the existing
     `!c->isfloating` guard) — it was only ever firing on spawn/reflow.
     **Fix: always commit.** Worst case is a stale buffer stretched into a
     new box for a frame or two on the one client that hasn't caught up yet
     — far better than freezing the whole output.
- Build clean, 40/40 unit tests green. User-confirmed on real hardware after
  this fix: noticeably faster, "usable even."

## 2026-07-07 — feature: Super+Ctrl+Left/Right peels a window out of a vertical strip

- Extended the same `move_window_dir()` (`code/src/modules/layout/layout_world.c`)
  change: when the focused window is in a vertical strip (has a same-column
  neighbour, below preferred else above — new `column_neighbor()` helper),
  Left/Right now peels it out and lands it beside that neighbour at the
  neighbour's own rank, instead of the old "swap with nearest window that
  way" behaviour. Alone in its column (no strip), Left/Right is unchanged
  (swap with nearest neighbour / open a new column at the edge).
- Two real bugs caught by VM-testing with labelled terminals (typed `echo
  ONE`/`echo TWO` into each so identity was unambiguous across screenshots —
  plain blank prompts made an early pass mid-investigation look like it had
  spawned an extra phantom window; it hadn't, that was the
  session's auto-launched startup terminal, a red herring):
  1. First attempt computed the target x as `ref->x ± (column_width + gap)`
     — a full grid step. `arrange_columns()`'s Pass 2 always re-snaps every
     client's `world.x`/`world.y` to a compacted, *uniformly-spaced* grid on
     every arrange, so a full-step offset from ref lands exactly on top of
     whatever column already happens to sit at that natural grid slot —
     indistinguishable from "merge into that unrelated existing column."
     Right happened to pass by accident (nothing was there); Left failed
     twice, merging into the auto-launched terminal's column instead of
     landing beside the intended neighbour.
  2. Fixed by reusing the fractional-offset trick already established in
     this file for spawn placement (`place_window_column()`:
     `f->world.x + f->geom.width * 0.5f + 1.0f`) — a half-width-plus-epsilon
     step sorts immediately adjacent to ref without coinciding with any
     full-grid-step column, guaranteeing a genuinely new column rather than
     an accidental merge. VM-reverified both directions against a strip with
     a third, unrelated pre-existing window in the collision-prone slot —
     both now land beside the intended neighbour only.
- Build clean, 40/40 unit tests green.

## 2026-07-07 — feature: Super+Ctrl+Up/Down always jumps to the left column; camera chases moved windows

- Changed `move_window_dir()` (`code/src/modules/layout/layout_world.c`):
  up/down used to conditionally swap with the same-column neighbour if one
  existed, only consuming into the left column (placed on top) when already
  at the top of the column — down had no equivalent fallback at all (no-op
  at the bottom). Inconsistent: whether Up "elevated" into the left column
  or just reordered locally depended on invisible column state.
- New rule, unconditional: **Up always jumps into the column immediately to
  the left, landing above everything there; Down always does the same,
  landing below everything there.** No left column: no-op (unchanged). Added
  `column_max_y()` (mirrors the existing `column_min_y()`) for the "bottom"
  placement; removed `stack_neighbor()`, now dead.
- Also added: **the camera now chases the window** on every Super+Ctrl+arrow
  move (`viewport_center_on(c)` after the position update) — up/down/left/right
  all keep the carried window in view instead of it potentially leaving the
  viewport unnoticed.
- Build clean, 40/40 unit tests green (no mock reimplements this placement
  path, so nothing needed updating there — see the general note on this under
  [[stability]]/test conventions).

## 2026-07-07 — docs: protocol gap audit + modularization rule, before any protocol code

- An agent testing kalinwm found `xdg-toplevel-icon-v1` unimplemented (log:
  `compositor does not implement the xdg-toplevel-icon protocol`, every
  session). Asked to fix it, but first: document the gap and the ground rule,
  since this is exactly the kind of addition that could grow `dwl.c` again
  right after "modularization step 1/2" started shrinking it.
- Audited every `wlr_*.h` protocol wrapper wlroots 0.20 ships against what
  `dwl.c` actually calls in `setup()`. Confirmed three gaps directly from our
  own log (`xdg-toplevel-icon-v1`, text-input-v3/input-method-v2,
  `xdg-system-bell-v1`) and identified six more popular-but-unimplemented ones
  (tearing-control-v1, content-type-v1, xdg-dialog-v1, xdg-foreign-v1/v2,
  keyboard-shortcuts-inhibit-v1, security-context-v1). Full breakdown, plus
  what's deliberately skipped and why, now lives in the new [[protocols]] note.
- New rule recorded in [[roadmap]] and [[dwl-fork]]: protocol work (starting
  with `xdg-toplevel-icon-v1`) goes in a new `code/src/modules/protocols/`
  directory. `dwl.c` keeps only the `wlr_*_create()` line in `setup()` — no
  new listener/logic added to the monolith.
- No code changed yet — this entry is docs only; implementation starts next.

## 2026-07-06 — fix: cropped window content stretched instead of showing 1:1

- Reported bug: after `Super+C` crop-select, the window shrank to the selected frame size but its *content* rendered as if zoomed/stretched to fill the window's original full size, with a few extra rows/columns bleeding past the small frame border.
- Root-caused in the VM with targeted `wlr_log` probes (not guessable from reading the code — the compositor-side clip/geometry math in `resize()` was already numerically correct).

- **Root cause (`code/src/dwl.c`, the crop rendering in `resize()` / `client_set_buffer_scale`):** `wlr_scene_subsurface_tree_set_clip()` selects a *source* sub-rectangle from the client's buffer, but `wlr_scene_buffer_set_dest_size()` then scales *that selected sub-rectangle* to fill dest_size — they're a matched pair, not independent knobs.
- `client_set_buffer_scale()` (shared with the zoom-crispness feature, re-applied every frame because wlroots resets a buffer's dest_size on every surface commit) was computing dest_size from the client's *full, uncropped* surface size regardless of crop — so a small clipped selection got stretched up to fill the full original window's worth of pixels.
- Confirmed by forcing a 20x20 clip and watching it blow up into giant blocky glyphs filling most of the window.

- **Fix:** `client_set_buffer_scale` now multiplies the zoom scale by the crop fraction (`c->crop.w`/`c->crop.h`) when the client is cropped, so dest_size matches the clipped region's own size (1:1, times zoom) instead of the full surface.
- `client_scale_buffers` took separate `scale_w`/`scale_h` (previously one uniform `scale`) to carry this.
- A second, smaller bug surfaced once the stretch was fixed: a few rows/columns still bled out above the frame — a stale position-shift compensation in the crop branch (sized for the old "display at full scale" approach, shifting `scene_surface`'s node by the crop offset in *full-scale* pixels) was now wrong given dest_size already shrinks to the crop's own size; removed it.
- The node now just sits at the frame's content origin (`z_bw, z_bw`) like the uncropped case, and the clip's `(x, y)` offset alone selects the correct source sub-region.

- VM-verified with a large monospace text grid (to make any stretch obvious): crop selection now shows at correct font size, no bleed past the frame, stable across repeated frames/re-renders.
- Unrelated behaviors (launcher, uncropped windows) unaffected.
- Build clean, 20/20 unit tests green.

## 2026-07-06 — modularization step 2: extract keyboard event dispatch to modules/input/keyboard.c

- Moved `keypress`, `keypressmod`, `keyrepeat` out of `dwl.c` into a new `code/src/modules/input/keyboard.c` TU (Makefile: added to `SRCS`).
- These are the actual per-event logic: gesture feeding into the bind engine, Super-held IPC broadcast, crop-mode 'r' intercept, and repeat scheduling.

- `keybinding()` (the compiled `keys[]` fallback dispatcher) and the wlroots keyboard-group lifecycle (`createkeyboard`, `createkeyboardgroup`, `destroykeyboardgroup`) **stay in dwl.c** — first attempt moved all 7 functions, but `keybinding()`'s fallback loop closes over `keys[]`, whose entries are function pointers to dozens of `static` action functions (spawn, focusstack, zoom, killclient, setlayout, ...).
- Extracting it would have forced de-staticizing every compiled keybind action just to satisfy the linker — a much bigger, riskier change than "extract keyboard input" implies, and it would have undone the "minimize extern surface" goal rather than serve it.
- `keybinding()` was instead made non-static (called cross-TU from keyboard.c) and stays put; the group lifecycle functions are pure wlroots boilerplate coupled to `config.h`'s xkb_rules/repeat_rate/repeat_delay, so they stay with the config they read.

- Mechanically this was low-risk: `kalin.h` already had a `DWL_INTERNAL`-gated extern block anticipating most of this (KeyboardGroup type, `kb_group`/`seat`/`locked`/`idle_notifier` externs, even the 6 function prototypes) — evidence a prior pass scaffolded the interface but never finished the extraction.
- Needed: de-static `locked`, `idle_notifier`, `seat` in dwl.c (kb_group reverted back to static since it turned out unneeded cross-TU); add a missing `ipc_broadcast_state` and `viewport_fit_all` prototype to kalin.h (gaps in the pre-existing interface).
- Also deleted `code/include/input.h`, an orphaned, unused, never-included header from an earlier abandoned extraction attempt (duplicated the `Key`/`Button` typedefs already in kalin.h; the only file still referencing it was a dead backup under `backups/`).

- Build clean, 20/20 unit tests green.
- VM-verified: tap Super → launcher, Super+T chord → foot (no launcher), hold Super 1.3s → window menu — all unchanged after the split.

## 2026-07-06 — modularization step 1: unify modifier tap/hold gestures in the bind engine

- Consolidated all modifier-gesture timing into the bind engine (was split: the Super-**tap** launcher lived hardcoded in `dwl.c:keypress()`, the **hold** menu in `bind_engine.c` — both independently tracked Super).
- Now the engine's gesture state machine owns both edges via one feeder, `bind_gesture_key(mods, is_modifier_key, pressed, time_msec)` (renamed from `bind_hold_key`, +timestamp for tap duration).
- `bind_gesture_interrupt()` cancels an arming gesture on a pointer-button press (replaces the old `super_tap.consumed` flag).

- New `tap` dispatch alongside `hold`: `find_hold_bind` → `find_gesture_bind(mods,
  edge)`; a single modifier can carry both a tap and a hold bind. Tap fires on a
  press+release within `BIND_TAP_MAX_MS` (250) with no intervening key/button.
- New action `toggle-launcher` (ACT_TOGGLE_LAUNCHER, strv arg): spawn the launcher
  or kill its process group if already up — the toggle-close behavior that used to
  be inline in `keypress`, now a first-class action so `tap Super -> toggle-launcher
  fuzzel` in the DSL fully replaces the hardcoded path.
- `keypress` shrank: the whole super_tap struct + tap-spawn block removed; it now
  just feeds the engine and broadcasts `super_held`. `binds_load` also clears
  `tap_armed` (dangled into the freed engine on hot reload).
- Test: `tap_bind` (20 total, all green). VM-verified: tap→launcher, tap→close,
  Super+T chord doesn't fire the tap, hold 1.3s→menu w/ stationary camera,
  release→menu hides. (One stuck-menu observation was a dropped QMP
  input-send-event up, not a compositor bug — a real key event cleared it via the
  normal release path.)

## 2026-07-04 — freeze investigation: neuter spotlight zoom, clamp camera, fix hold UAF

- Reported symptom: whole session freezes (cursor included) when holding Super /
  the menu appears; can't even Super+Escape quit. Not reproducible in the
  [[test-vm]] (software renderer) — every path incl. double-Super+Escape quit
  works there. Diagnosis: a **real GPU driver hang** from extreme/NaN scene
  coordinates, which the VM's software path tolerates.
- **Root cause:** the buggy spotlight camera-zoom (save/restore of a moving
  target) could push `viewport.zoom` to a bad value → enormous WORLD_TO_SCREEN
  coords → GPU lockup. The previous "drop the zoom" fix was **shell-side only**;
  a not-yet-rebuilt Quickshell still sends the `spotlight` IPC command.
- **Fixes:** (1) the compositor's `spotlight` IPC command is now a **no-op**, so
  no shell version can trigger the zoom (`ipc.c`). (2) `viewport_tick()` clamps
  zoom to a finite [0.05, 20] every frame — cheap insurance against any camera
  bug freezing a real GPU (`viewport_ops.c`). (3) Fixed a **use-after-free**:
  the hold gesture held raw `Binding*` into the bind table, which config
  hot-reload frees; `binds_load()` now drops the in-progress hold first.
- Action for real hardware: rebuild **both** kalin-wm and quickshell, then
  relogin. Build clean; 18+19 tests green.

## 2026-07-04 — hold-to-show window menu via the bind DSL; drop the buggy zoom

- **`hold` gesture dispatch in the bind engine.** A modifier-only `hold` bind
  (e.g. `bind hold Super -> window-menu`) fires after the modifier is held for
  `hold_ms` (default 1000; DSL override `hold 1500 Super`) **uninterrupted by
  another key press**, and a paired release fires when the modifier lifts.
  Implemented with a `wl_event_loop` timer in `bind_engine.c`
  (`bind_hold_key()`); `keypress()` feeds every key event with an
  `is_modifier_key` flag. Independent of press-chord dispatch. New
  `bind_invoke_release()` in `dwl.c` is the release half of while-held actions.
- **`window-menu` action + `menu` IPC flag.** `ACT_WINDOW_MENU` sets/clears a
  new `menu_shown` global, broadcast in `ipc_build_state()`. The timing/logic
  lives entirely in the bind file + engine; the compositor just exposes the
  show/hide.
- **Dropped the spotlight camera-zoom from the menu.** It saved a *moving*
  animation target and snapped the camera to the wrong place on release.
  `WindowActions.qml` no longer runs its 180ms `holdTimer` or calls
  `spotlight()`; it mirrors `KalinViewport.menuShown` and shows the radial in
  place. `spotlight_enter/exit` remain in the tree, just untriggered.
- **Cancel rule:** another key press (or releasing Super early / adding a
  modifier) cancels the arming; mouse clicks don't. Super-**tap** launcher is
  unchanged and coexists (tap<250ms → launcher, hold≥1s → menu).
- Tests: +2 parser cases (`hold Super`, `hold 1500 Super`); suite 18+19 green.
  Note: existing user `binds.conf` needs regenerating to gain `hold Super` and
  the `move-window` binds (the compositor never overwrites a valid file).

## 2026-07-04 — 2D graph tiling: 4-way window movement + adaptive spawn memory

- **Carry a window through the grid.** New `move_window_dir()` in
  `layout_world.c`, bound to `Super+Ctrl+Arrows` (added to `default_binds.h`;
  DSL action `move-window <dir>`). up/down reorder within the column stack;
  **up at the top consumes into the left column, on top** (the user's rule);
  left/right **swap** the focused window with the nearest neighbour that way so
  it walks across the grid (new column if there's no neighbour); down at the
  bottom edge is a no-op. Reuses a new `nearest_in_direction()` factored from the
  directional-focus cone search (`dwl.c`).
- **Spawn relative to focus.** `place_window_column()` now opens a new window to
  the **right of the currently-focused window** (reading the previously-focused
  entry from `fstack`, since the new client is already at its front) instead of
  always the far-right column.
- **Adaptive spawn cursor ("memory of last relation").** A small `SpawnCursor`
  in `layout_world.c`: after a vertical move it flips to `SPAWN_STACK_TOP` so the
  **next window stacks on top of that column**; `focusclient` resets it (via
  `spawn_cursor_on_focus`) once focus leaves the column. Window *positions*
  already persist across restarts via world coords in `persistence.c` — no change
  there. De-static'd `fstack` for the layout TU.
- **Tests:** +2 parser cases (`move-window` arg parse + bad dir); suite 18+17
  green; changed files build warning-clean. Verified live in the [[test-vm]].

## 2026-07-04 — runtime bind engine + custom bind DSL (Stage 1)

- **Decision: a custom DSL, compositor-owned.** Keybinds move from compile-time
  `static const keys[]`/`buttons[]` (raw function pointers) to a runtime engine
  reading `~/.config/kalin-wm/binds.conf`, hot-reloaded on save via inotify. The
  compositor owns parse + dispatch (it's the only thing that sees raw input
  first); a future Quickshell UI (Stages 2–3) will edit over IPC. Format is a
  purpose-built DSL: `bind [tap|hold] Mods+Key -> action args`, `mode name { }`
  blocks, and reserved `gamepad`/`swipe.*` triggers. See [[bind-engine]].
- **New module `code/src/modules/binds/`:** `bind_actions.c` (name→ActionId
  registry + pure arg parsers, no wlroots — unit-testable), `bind_parser.c`
  (DSL lexer/parser → heap `BindEngine`), `bind_engine.c` (table swap, dispatch
  with active-mode→default fall-through, file load, embedded default, inotify
  reload). Types in `code/include/binds.h` (self-contained; `Arg` guarded so it
  co-includes with `kalin.h`). `bind_invoke()` in `dwl.c` maps ActionId→the
  existing (often static) action functions, translating semantic ints
  (directions, layout index) to the wlroots enums/pointers.
- **Stage 1 scope:** dispatches single-step chords, pointer buttons, scroll
  (closes the old `axisnotify` TODO), and mode switching. Tap/hold, leader
  sequences, gestures, and `gamepad` lines are *parsed and stored inactive* so
  the file format is frozen ahead of Stages 4–5. `keybinding`/`buttonpress`
  fall back to compiled `keys[]`/`buttons[]` only if no engine loaded (safety
  net; a bad user file also falls back to the embedded default).
- **Tests:** 14 new parser unit tests (`code/tests/test_binds.c`, links
  xkbcommon) covering chords, literals, pointer/scroll, mode blocks, sequences,
  arg parsing, and error paths. Suite 18+14 green; bind modules build warning-clean.
- Remaining: Stage 2 IPC CRUD + capture, Stage 3 Quickshell editor, Stage 4
  sequences/tap-hold/gestures dispatch, Stage 5 evdev gamepad. Compiled
  `keys[]`/`buttons[]` retired once DSL parity is battle-tested.

## 2026-07-03 — smooth animation + crisp high-res zoom

- **Frame-synced window glide.** The window spring now steps inside
  `rendermon()` (vsync-aligned with the camera) instead of a free-running 16 ms
  timer, and each frame only repositions the client's scene node (borders/focus
  ring are its children) rather than calling the full `resize()`; `resize()`
  runs once on settle. Fixes the janky, unsynced glide.
- **Crisp zoom via fractional-scale.** Real fix for "zoom is pixelated": the old
  path upscaled the native buffer. On camera *settle* the compositor now asks
  each client to render at `output_scale * zoom` via `client_set_scale`
  (wp_fractional_scale, already wired), capped at `zoom_render_max`. `dest_size`
  uses the surface *logical* size × zoom (resolution-independent), so it's right
  whether the client rendered at 1× or zoom×. Verified in the [[test-vm]]: foot
  glyphs are sharp at deep zoom. Caveat: only fractional-scale-aware clients
  (foot, most GTK/Qt) get crisp; others stay soft.
- **4K screenshot on an HD panel (`Super+Print`).** New `modules/capture.c`:
  renders the focused view to a throwaway **2× headless `wlr_scene_output`**
  (sharing the scene, allocator, renderer), reads the buffer back with
  `wlr_texture_read_pixels`, and writes a PNG via a **self-contained encoder**
  (stored/uncompressed deflate — no zlib in the dev shell). Lands in
  `$KALIN_SHOT_DIR` or `$HOME`. Verified in the [[test-vm]]: a valid 2560×1600
  PNG of the real desktop (= 3840×2160 from a 1080p panel). Files are large
  (~12 MB @ 2560², ~33 MB @ 4K) since uncompressed — adding zlib to the flake
  would shrink them ~10×.
- **Note:** the fluid/zoom code keeps growing `dwl.c`; still owes a
  `modules/anim.c` extraction.

## 2026-07-03 — Niri-style fluid feel + spotlight window-actions

- **Window spring-glide (Phase 1).** Windows animate their world position toward
  the layout target instead of snapping: each [[dwl-fork|Client]] carries
  `target_geom` + spring velocity, `arrange_columns()` calls the new
  `client_set_target_geom()`, and an event-loop timer springs them via
  `spring_step()`. Size is applied instantly (position-only avoids per-frame
  reconfigures and layout width-feedback); camera pans stay instant. Tunable via
  `anim_stiffness`/`anim_damping`. Verified in the [[test-vm]]: `move_column`
  shows windows mid-slide.
- **Win+drag → snap to nearest column (Phase 2).** On `CurMove` release a dragged
  window re-tiles at the drop position (`nearest_column_x()`); Phase 1 glides it
  in. Required making the hold-Super radial menu **click-through** (empty input
  mask) — its overlay had blocked the drag.
- **Hold-Super spotlight + buttons from the window (Phase 3).** The shell drives a
  spotlight over IPC (`spotlight 1/0`, after its 180 ms debounce): camera zooms to
  the focused window (`viewport_focus_window`) and the rest dim (`spotlight_dim`),
  restoring on release (`viewport_animate_to`). The compositor broadcasts the
  focused window's on-screen `rect` and the radial buttons flow out of the window
  perimeter. Verified: hold → 106% zoom + buttons around the window; release →
  100%, undim.
- **Phase 4 (camera spring) deferred** (existing easing already fluid); **language
  call: stay C** (a Rust/Smithay rewrite is months of re-plumbing for no
  user-visible gain).

## 2026-07-03

- **Phase 2 linkage split: 5 more modules extracted (blocker #2 cleared).**
  Promoted the three dwl.c-private anonymous structs to shared linkage — gave
  them named types (`Viewport`, `Wallpaper`, `CropEditor`) in
  [[dwl-fork|kalin.h]]'s always-visible TYPES section, made dwl.c own single
  external-linkage instances, and declared `extern` handles. That unblocked all
  five struct-coupled modules, now their own TUs: `viewport/viewport_ops.c`,
  `layout/layout_world.c`, `ui/wallpaper.c`, `crop/crop_mode.c`, and `ipc.c`.
  Additional de-static work along the way: `event_loop`, `printstatus`,
  `layers[]`, `cursor`/`cursor_mgr`, the `viewport_*`/`infinite`/`move_column`/
  `arrange_columns` entry points, and the shared `same_column_x` helper (crop
  calls it across the layout TU boundary). Moved the `CROP_*` visual macros into
  `crop_mode.c` (only used there). `nm` confirms one global def each of
  `viewport`/`wallpaper`/`crop_editor` with modules referencing them as `U`.
  Both build profiles pass; `make test` 18/18. Committed as five focused
  commits (one per module + the promotion).
  - **Only blocker #1 remains:** the two UI modules (`offscreen_indicators`,
    `overlay_clock`) stay `#include`d in dwl.c pending the `config.h`
    appearance-vs-keybind-tables split. Deferred by decision — it's a
    user-facing config.def.h change and not worth doing until it matters.

## 2026-07-02

- **Phase 2 linkage split begun (true separate compilation).** Chose real
  separate compilation over a cosmetic `#include` split, for enforced module
  interfaces + fast incremental builds. Converted two modules from
  `#include`d-into-dwl.c to their own TUs: `input/resize_actions.c` and
  `foreign_toplevel.c` (joining the pre-existing `input/commit_size.c`). Pattern:
  the module `#include`s [[dwl-fork|kalin.h]] *without* `DWL_INTERNAL` (so it
  sees the extern interface), dwl.c drops `static` from exactly the globals +
  functions that module references, kalin.h declares them, and the Makefile
  `SRCS` gains the file. No namespace clashes on generic names (`resize`,
  `arrange`, `selmon`, `mons`…). Added include guards to `util.h` and
  `client_inline.h` so they survive multi-TU inclusion.
  - **Two structural blockers found for the remaining modules.** (1) The UI
    modules (`offscreen_indicators`, `overlay_clock`) need appearance constants
    that live in `config.h` — but `config.h` also holds the keybind/button
    tables, whose function pointers force *every* keybind action to external
    linkage; the `-O1` build hid this via dead-code elimination but `make debug`
    (`-O0`) exposed it. Extracting them cleanly needs `config.h` split into
    appearance-constants vs keybind-tables (a config.def.h change). (2) Five
    modules (`crop_mode`, `layout_world`, `wallpaper`, `viewport_ops`, `ipc`)
    reference dwl.c-private structs (`viewport`, `wallpaper`, `crop_editor`),
    which must be promoted to shared linkage first. Both parked pending a design
    decision; the two clean extractions are committed and both build profiles
    (release + debug) pass 18/18 tests.
- **Phase 2 foundation: single shared data model.** dwl.c no longer duplicates
  the type definitions — it now `#include`s [[dwl-fork|kalin.h]] for the data
  model (enums, `Arg`/`Button`/`Key`/`Layout`, `Client`, `Monitor`,
  `LayerSurface`, `KeyboardGroup`, `MonitorRule`/`Rule`/`PointerConstraint`/
  `SessionLock`) and its shared macros. dwl.c defines `DWL_INTERNAL` before the
  include so it pulls in **only the types** and skips kalin.h's `extern` globals
  + prototypes (which would clash with dwl.c's file-scope statics — those stay
  owned by dwl.c until the linkage split). Added an include guard to `util.h`
  (it defined `static inline` helpers with no guard and was now pulled in
  twice). Struct edits now happen in exactly **one** place. Build clean, 18/18
  unit tests green.
- **Fixed the keyboard-focus daily-driver blocker.** Spawned windows couldn't be
  typed into because the Quickshell bar held an exclusive keyboard grab.
  - Compositor: `arrangelayers` treated the layer-shell `keyboard_interactive`
    enum as a boolean (grabbing for ON_DEMAND, not just EXCLUSIVE) and never
    handed focus back when a layer released the grab. Both fixed.
  - Shell ([[quickshell-shell]] `BottomBar.qml`): only grab (Exclusive) when the
    launcher is opened by click (`leftPinned`); use None at idle. Quickshell
    0.3.0 sends `OnDemand` to the compositor as an exclusive grab, so idle
    windows were starved. Verified in the [[test-vm]]: typing lands in the
    focused window.
- **Per-window opacity.** New `Client.opacity` (0.1–1.0) applied to scene buffers
  and re-applied on commit; `Super+D` / `Super+Shift+D` to dim/brighten. First
  piece of the [[window-menu]] (which will be a Quickshell surface, alongside
  the keybinds).
- **NiriIpc noise silenced.** The shell was polling `niri msg` every second even
  on kalin-wm (isKalin is actually true; the taskbar already works via
  [[foreign-toplevel]]). Gated the poll on `!KalinViewport.enabled`.
- **`kalinwm` dev launcher** added to `home-config/display.nix`: runs the local
  working-tree build with the shell + terminal on the current TTY, logs to
  /tmp/kalinwm.log. The `ly` "kalin-wm" session still uses the pinned flake build.
- **Refactor toward fewer files (Phase 1 done, Phase 2 begun).** Deleted the dead
  parallel headers (`layout.h`/`client.h`/`compositor.h`) — struct edits went
  from 4 places to 2. Reconciled [[dwl-fork|kalin.h]]: removed drift (stale
  world<->screen macros) and orphan Viewport/Wallpaper/CropEditor typedefs that
  would collide when dwl.c starts including kalin.h. Next: dwl.c includes kalin.h
  for the data model, then split dwl.c into translation units.
- **`~/environment/quickshell` is now a git repo** (was untracked); initial
  import captured the shell + this session's fixes.

## 2026-07-01

- **Removed three dwl subsystems** (one commit, build + 18 unit tests green,
  verified running in the [[test-vm]]):
  - **XWayland** — was already compiled out; deleted all `#ifdef XWAYLAND`
    code and the X11 client abstraction. kalin-wm is now Wayland-only (see
    [[dwl-fork]], [[build-system]]).
  - **Tiling params** — dropped `mfact`/`nmaster` and the never-defined
    `monocle`/`tile` layouts; only `infinite` + floating remain. Un-shadowed
    `Super+I` (had been masked by `incnmaster`).
  - **The 9-tag workspace system** — collapsed `VISIBLEON` to same-monitor
    ("one [[infinite-canvas]] per monitor, always"); removed tags/tagset,
    view/tag/toggleview/toggletag, the stdout `tags` status, and the tags field
    in [[persistence]]. Un-shadowed `Super+0` (fit-all) and freed `Super+Tab`.
    The [[quickshell-shell]] was unaffected (reads the [[ipc-socket]] JSON +
    [[foreign-toplevel]], never the stdout status).
- **Re-scoped the project (see [[roadmap]]).** Daily-driver goal on the
  infinite canvas; horizontal column scrolling is the primary motion.
  Priorities: [[stability]] → [[quickshell-shell]] → continued dwl cleanup.
  [[zoom]] parked. New planned feature: the [[window-menu]] (per-window action
  menu — resize, opacity, anchor, close, fullscreen).
- **Host-driven [[test-vm]] automation.** Added `test-vm/scripts/vmctl.py`
  (`up`/`shot`/`key`/`type`/`down`): QMP for input, VNC-framebuffer read for
  screenshots (QMP `screendump` fails "no surface" on the GL scanout).

## 2026-06-30

- **Automated real-VT test runner on tty3.** Added `scripts/test-tty3` which uses
  `openvt -c 3` to start kalin-wm + Quickshell + `foot` on a real virtual terminal,
  captures logs to `/tmp/kalin-tty3-test/`, and switches back to the original VT on
  exit or timeout. Added `"tty"` to the user's `extraGroups` in
  `~/home-config/users.nix` so `openvt`/`chvt` can access VTs.
- **Zsh aliases for the whole workflow.** Added `kalin-*` aliases to
  `~/home-config/desktop.nix` covering navigation (`kalin-code`, `kalin-shell`,
  `kalin-vm`, `kalin-home`), build/test (`kalin-build`, `kalin-test`), runners
  (`kalin-nested`, `kalin-tty`, `kalin-tty3`), the test VM (`kalin-vm-build`,
  `kalin-vm-run`, `kalin-vm-logs`), and host rebuilds (`kalin-rebuild`,
  `kalin-rebuild-build`). The NixOS configuration builds; a `sudo nixos-rebuild
  switch` and a new terminal are needed to activate both the `tty` group and the
  aliases.
- **Display settings panel added to the quickshell shell.** A new right-side
  system-panel tab (opened from the bottom-bar display icon) lists connected
  outputs and lets the user reorder them left-to-right under niri. It uses a new
  `DisplayService` singleton that reads `niri msg --json outputs` and applies
  positions via `niri msg output <name> position set <x> <y>`. On kalin-wm the
  tab shows a placeholder because kalin-wm has no runtime output IPC yet.
- **Fixed kalin-wm layer-shell crash.** Spawning any layer-shell client (`fuzzel`,
  the quickshell bar, notification popups) crashed the compositor on surface
  destruction. `destroylayersurfacenotify()` was double-freeing the scene tree
  owned by `wlr_scene_layer_surface_v1_create()`; removed the manual destroy of
  `l->scene->node` and kept only the popup-tree cleanup. Verified `fuzzel` and
  the quickshell shell now start under the nested compositor without crashing.
- **Updated stale keybinding docs.** `README.md` and `scripts/run-tty` still
  referenced old `Super+Shift+Return` / `Super+Shift+Q` bindings; corrected them
  to the current `Super+T` terminal, `Super+P` launcher, and `Super+Escape` quit.
- **Ported the quickshell taskbar to the kalin-wm backend.** `TaskbarService.qml`
  now reads `CompositorService.windows` and uses `CompositorService.activate()` /
  `close()` so pinned/running app buttons work on both niri and kalin-wm.
  `WorkspaceIndicator.qml` is hidden on kalin-wm (no fixed workspaces).
- **Auto-start the shell + terminal in the kalin-wm login session.** The
  `kalin-wm.desktop` session now uses a `kalin-wm-session` wrapper that exports
  `QS_CONFIG_PATH=/home/kalin/environment/quickshell` and launches
  `kalin-wm -s 'qs & foot --server'`. Validated by booting the `~/environment/test-vm`
  NixOS VM headless (`QEMU_OPTS="-display egl-headless,gl=on"`); the compositor
  started on `/dev/tty1` and Quickshell reported `Configuration Loaded`.
- **Fixed test-VM virtio-serial log permissions.** The autologin user could not
  write `/dev/virtio-ports/{kalinlog,qslog}`, so host log files stayed empty.
  Added udev rules in `~/environment/test-vm/vm.nix` to give `tester` ownership of
  those ports. Also documented the headless smoke-test command and the
  `nix flake update kalin-wm` step.

## 2026-06-26

- **Docs consolidated into this vault.** All project docs were folded into a flat
  `obsidian/` model at the repo root. The prior research vault moved from
  `docs/obsidian-vault/research/` to [[research/README|research/]] and is now a
  linked subtree. Operational docs (ROADMAP, AGENTS, CURRENT_SPECS, changelog,
  incidents, READMEs) were distilled into object notes plus this ledger.
- **kalin-wm wired into the host as a login session** via the [[nixos-session]]
  (`~/home-config`, `display.nix`). Builds clean; not yet activated with
  `nixos-rebuild switch`. The session currently launches the compositor bare —
  it does not yet start the [[quickshell-shell]] + a terminal.
- **VM logging added** to the [[test-vm]] (virtio-serial → host files). Booting
  showed the compositor renders and [[keybindings|Super+T]] spawns a terminal,
  but the [[quickshell-shell]] is still niri-bound: `TaskbarService` and
  `WorkspaceIndicator` call `niri msg` unconditionally and do nothing on
  kalin-wm. **Next task:** migrate those widgets onto the kalin-wm backend
  ([[foreign-toplevel]] via `CompositorService`) and fix [[overview-mode]]
  capture.
- ROADMAP P1 trimmed: dropped smooth animations and touchpad gestures.
- Docs fix: corrected stale source-layout tables (they listed files that don't
  exist; the build is [[dwl-fork|dwl.c]] `#include`-ing the modules).

## 2026-06-20 — viewport & shell-integration work

- [[zoom]] became a true scene scale; added [[fit-all]] navigation (`Super+0`).
- Smooth frame-rate-independent camera + animated [[zoom]].
- Added the [[ipc-socket]] for shell camera control.
- Implemented [[foreign-toplevel]] (wlr-foreign-toplevel-management-v1).
- Baked runtime library RPATH into the [[build-system|nix package]].
- `Super+O` keybind added to toggle the [[overview-mode]] via shell IPC.

## Earlier

- **Stability audit** identified 23 issues (4 critical, 8 high). All Phase 0
  items were fixed and re-verified against the live code. See [[stability]].
- **Build/buffer-scaling fixes:** removed a duplicate `same_column_x()` that
  broke the build; fixed [[buffer-scaling]] that was silently disabled by a
  wrong node comparison in `client_set_buffer_scale()`.
- **Spawn crash fixed:** compositor crashed when spawning a terminal with 2+
  windows open, due to `resize()` touching an under-initialized client. Fixed
  with NULL/geometry guards. See [[stability]].
- Reorganized the source layout into [[dwl-fork|dwl.c]] + runtime modules;
  generated protocol headers via wayland-scanner.
- Initial dwl import (the fork point). See [[dwl-fork]].
