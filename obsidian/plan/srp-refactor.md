# SRP audit + slimming plan

Audit 2026-08-12 (Claude + a subagent SRP sweep). Goal: a repo worth showing off
as well-architected. Ordered strictly **low-risk-first** — dead code + pure moves
(build+unit only) before anything touching the render/input hot paths (VM-gated).

`code/src/dwl.c` is the god-file: **4969 LOC** at audit time, ~315 functions. Modules
already exist under `code/src/modules/`; some are separate `.o` (SRCS in Makefile),
some are `#include`d into dwl.c's TU (crop/layout/ui/viewport bits + offscreen_indicators.c).

## Responsibility clusters in dwl.c (for future extractions)
- cursor/pointer input (`motionnotify` 210, `buttonpress` 163, `motionabsolute`, cursor grabs)
- output/monitor mgmt (`createmon`, `updatemons` 107, `outputmgrapplyortest`, `ipc_set_output`, `monitor_find_by_name`)
- client lifecycle (`mapnotify` 246, `unmapnotify` 110, `destroynotify`, `commitnotify`, `resize` 95)
- rendering + the zoom/buffer-scale machinery (`rendermon`, `client_apply_zoom_frame`, `client_scale_buffers`, `client_apply_zoom_scale`)
- seat/focus, xdg/layer-shell wiring, `setup` 212 / `run` 95.

## Done — Etap 1 (2026-08-12, commit on branch → main)
- **Dead code removed** (pure no-op): the disabled hold-Super **spotlight** subsystem (`spotlight_enter/exit` + statics + the `spotlight` IPC command + `spotlight_dim` + `viewport_focus_window`, which only spotlight called); `wallpaper_init` (declared, never defined/called); 4 `DEBUG:` comment stubs.
- **camera-bypass predicate unified** → `client_is_camera_bypassed()` (`client_inline.h`). The 4 copies had diverged on `isfloating` (missing in `client_anim.c` + `offscreen_indicators.c`) — a latent double-transform / spurious-off-screen-marker bug for floating overlays. Fixed.

## Remaining (fresh sessions, low-risk-first)

### Etap 2 — pure moves (build+unit)
- ~~**tmux-spawn cluster**~~ **DONE 2026-08-12** → `code/src/modules/spawn/tmux_spawn.c` (#include'd into dwl.c, same TU). Also fixed the `static void spawn` decl inconsistency (dwl.c doesn't include kalin.h). dwl.c 4912→4796.
- ~~**Output/monitor module**~~ **DONE 2026-08-12** → `code/src/modules/output/output.c` (#include'd, same TU): createmon/cleanupmon/closemon/updatemons/requestmonstate + outputmgrapply/ortest/test/powermgrsetmode + ipc_set_output/monitor_find_by_name. rendermon/setmon/focusmon/tagmon stayed (render/focus, not output). dwl.c 4796→4367. VM-smoke passed. (ipc_set_output correctly went here, NOT ipc.c as the audit had said.)
- ~~**dock-prep cluster**~~ **DONE 2026-08-13** → `code/src/modules/dock/dockprep.c` (#include'd, same TU): `dockprep_pending` table + `dockprep_register`/`dockprep_consume`. Byte-identical pure move; both already public (kalin.h) for ipc.c, so dwl.c keeps two forward-decls near `dock_hover_client`. dwl.c 4367→4309. Build clean + test 25/25 (VM-smoke skipped: byte-identical move on the branch output-module already boot+render verified).

**Etap 2 zamknięty.** dwl.c 4969 (start SRP) → 4309 (−660, ~13%), rozbite na `spawn/tmux_spawn.c`, `output/output.c`, `dock/dockprep.c` + usunięty martwy kod. Dalej: Etap 3 (`mapnotify`/`motionnotify`/`buttonpress` decomposition), Etap 4 (zoom-scale 3-5) — świeże sesje.

### Etap 3 — SRP decomposition (behaviour-sensitive, VM-gated)
- `mapnotify` (246), `motionnotify` (210), `buttonpress` (163): extract helpers. The input handlers' nested-mode `if` chain (`crop_editor.active` / `screenshot_ui.active` / `super_held` / `cursor_mode`) is the candidate for an explicit `InputMode` dispatch — highest risk, do last.

### Etap 4 — architectural debt
- The **three per-frame scale systems** that must agree every frame — steps 3-5 of [[zoom-scale-overhaul]] (drive scale from a commit listener, single cached-native source of truth). Blocked on real-Zen visual verification (the VM only runs `foot`/Firefox). Also reconcile the doc-vs-code claim at dwl.c:737 (`arrange()` at settle that doesn't happen).

### Duplication worth a shared helper (later)
- XRGB→PNG readback + nearest-neighbour scale (capture.c / toplevel_export.c / shaders.c) — one helper.
