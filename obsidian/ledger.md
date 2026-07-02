# Ledger

Running log of decisions, progress, and changes for [[kalin-wm]]. Newest first.
Dates are absolute.

## 2026-07-02

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
