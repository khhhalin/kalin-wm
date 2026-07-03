# Ledger

Running log of decisions, progress, and changes for [[kalin-wm]]. Newest first.
Dates are absolute.

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
