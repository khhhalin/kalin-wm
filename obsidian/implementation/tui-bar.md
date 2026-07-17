# TUI bar (kalin-bar-tui bar)

- **The bottom bar as a real terminal** — the endgame of the 2026-07-16 bar
  restyle session (typeset-statusline direction, B's bold type).
  **LIVE since 2026-07-17: the cutover shipped** — `BarConfig.useTuiBar`
  defaults true, the QML bar surface is deleted (quickshell commit
  `d984fb6`; `KALIN_TUI_BAR=0` now leaves the strip unreserved, it does NOT
  restore a QML bar). Hardware click test passed live (taskbar focus +
  panel toggles by hand); the floating-panels bug it surfaced was an IPC
  server bug, fixed (see below).
- **Architecture** (three pieces):
  - `tools/bar-tuis/kalin_tuis/bar.py` — a Textual app in the existing
    [[bar-tuis]] suite. Taskbar from the [[ipc-socket]] `clients` broadcast
    (diffed by signature so camera-move broadcasts don't churn image widgets);
    click sends `focus <id>`. cpu/mem psutil, battery `/sys`, volume `wpctl`
    polls; 1s clock. Typeset look: `⟨ ⟩` groups, `│` separators, glyph meters,
    `─┤ HH:MM ├─`.
  - **kitty hosts it** (`--class kalin-bar-<output>`): taskbar icons are real
    rasters over the kitty graphics protocol (textual-image `TGPImage`).
    foot+sixel corrupts rows under Textual; ghostty needs OpenGL 4.3 (Ivy
    Bridge has 4.2). kitty's `background_opacity` has the same
    matching-alpha semantics as foot's `alpha-mode=matching`, so
    `background=#1e1915` keeps the translucency story intact.
  - `quickshell/modules/BarHost.qml` — QML keeps only what a terminal can't
    do for itself: reserve the strip (PanelWindow exclusiveZone, transparent)
    and supervise the process (dockprep-before-spawn like DockedPanel, dock
    re-asserted 1/s through slow cold starts, respawn 2s after exit).
- **Flag**: `BarConfig.useTuiBar` — env `KALIN_TUI_BAR=1` (String() coercion;
  a strict `===` against `Quickshell.env()` silently evaluated false).
  `KALIN_BAR_WRAP=<script>` swaps the packaged command for a dev wrapper —
  `tools/bar-tuis/dev/kalin-bar-wrap.sh` (nix-shell kitty + textual-image).
- **The kitty-python PATH trap (cost a debugging hour, packaging must dodge
  it):** a bare `python3` under kitty resolves to kitty's own bundled
  interpreter (nixpkgs wrapper PATH-prefix), and even inside
  `nix-shell -p kitty -p python3.withPackages(...)` kitty's *propagated* bare
  python3 shadows the env one — `command -v python3` picks the wrong
  interpreter. The bar must be launched with the **absolute** withPackages
  path (the dev wrap greps `$buildInputs` for the `-env` store path; the P4
  packaged wrapper interpolates it at build time).
- **Debug note:** redirecting the bar's stderr blackholes the whole UI —
  Textual renders through that stream in this setup; capture only while
  hunting a crash, never in the standing command.
- **Cutover done (2026-07-17).** P4 packaged (`kalin-bar-kitty`, kitty NOT in
  systemPackages — bar-canvas only; barTuiEnv += textual-image; the flake
  input needed `nix flake lock --update-input kalin-wm` + re-switch, the
  first switch shipped a stale 10:18 snapshot without bar.py). Panel toggling
  ships: clicking a group dockpreps+spawns/re-docks its kalin_tuis panel
  (appids match the old DockedPanel convention; one panel at a time;
  KALIN_BAR_OUTPUT from BarHost names the output). QML bar deleted.
- **Bugs the gate caught (all fixed):**
  - BarHost's reservation surface ate every click (Top-layer over the docked
    kitty) → empty input `mask`.
  - Dock rect trusted boot-stale `screen.width` (VM: bar stuck at 640) →
    use the anchored window's own width + re-dock on change.
  - BarHost could spawn before the QML screen populated (empty appid) →
    screen-ready poll before first spawn.
  - **The compositor IPC server dropped one-shot senders' commands** (HUP
    race + greeting-EPIPE remove-before-drain) — the bar's dockprep never
    registered, so every panel mapped floating. Fixed in ipc.c
    (drain-and-execute on every disconnect path); this had silently affected
    kalin-dock CLI-style senders since the IPC existed.
- **Follow-ups (v1 losses + polish):** system tray (StatusNotifier pixmaps —
  drawable over TGP), MPRIS group, calendar (clock click → a calendar TUI
  panel), window peek, taskbar context menu + pins, hover-grace panel
  auto-close (dock_hover is still broadcast), bar "stretched" report from
  the nested test (likely nested-window scaling — re-check on live), general
  polish pass. Also: synthetic-pointer button drop (roadmap known-bugs)
  blocks click automation in VM/nested gates.
- Verified: nested (strip, dockprep, respawn self-heal, icons, meters,
  clock), VM (real DRM boot, bar up), live (hardware clicks: taskbar focus +
  panel toggles).
