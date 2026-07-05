# Ledger

Running log of decisions, progress, and changes for [[kalin-wm]]. Newest first.
Dates are absolute.

## 2026-07-06 — modularization step 2: extract keyboard event dispatch to modules/input/keyboard.c

Moved `keypress`, `keypressmod`, `keyrepeat` out of `dwl.c` into a new
`code/src/modules/input/keyboard.c` TU (Makefile: added to `SRCS`). These are
the actual per-event logic: gesture feeding into the bind engine, Super-held IPC
broadcast, crop-mode 'r' intercept, and repeat scheduling.

`keybinding()` (the compiled `keys[]` fallback dispatcher) and the wlroots
keyboard-group lifecycle (`createkeyboard`, `createkeyboardgroup`,
`destroykeyboardgroup`) **stay in dwl.c** — first attempt moved all 7 functions,
but `keybinding()`'s fallback loop closes over `keys[]`, whose entries are
function pointers to dozens of `static` action functions (spawn, focusstack,
zoom, killclient, setlayout, ...). Extracting it would have forced de-staticizing
every compiled keybind action just to satisfy the linker — a much bigger, riskier
change than "extract keyboard input" implies, and it would have undone the
"minimize extern surface" goal rather than serve it. `keybinding()` was instead
made non-static (called cross-TU from keyboard.c) and stays put; the group
lifecycle functions are pure wlroots boilerplate coupled to `config.h`'s
xkb_rules/repeat_rate/repeat_delay, so they stay with the config they read.

Mechanically this was low-risk: `kalin.h` already had a `DWL_INTERNAL`-gated
extern block anticipating most of this (KeyboardGroup type, `kb_group`/`seat`/
`locked`/`idle_notifier` externs, even the 6 function prototypes) — evidence a
prior pass scaffolded the interface but never finished the extraction. Needed:
de-static `locked`, `idle_notifier`, `seat` in dwl.c (kb_group reverted back to
static since it turned out unneeded cross-TU); add a missing `ipc_broadcast_state`
and `viewport_fit_all` prototype to kalin.h (gaps in the pre-existing interface).
Also deleted `code/include/input.h`, an orphaned, unused, never-included header
from an earlier abandoned extraction attempt (duplicated the `Key`/`Button`
typedefs already in kalin.h; the only file still referencing it was a dead
backup under `backups/`).

Build clean, 20/20 unit tests green. VM-verified: tap Super → launcher, Super+T
chord → foot (no launcher), hold Super 1.3s → window menu — all unchanged after
the split.

## 2026-07-06 — modularization step 1: unify modifier tap/hold gestures in the bind engine

Consolidated all modifier-gesture timing into the bind engine (was split: the
Super-**tap** launcher lived hardcoded in `dwl.c:keypress()`, the **hold** menu
in `bind_engine.c` — both independently tracked Super). Now the engine's gesture
state machine owns both edges via one feeder, `bind_gesture_key(mods,
is_modifier_key, pressed, time_msec)` (renamed from `bind_hold_key`, +timestamp
for tap duration). `bind_gesture_interrupt()` cancels an arming gesture on a
pointer-button press (replaces the old `super_tap.consumed` flag).

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
