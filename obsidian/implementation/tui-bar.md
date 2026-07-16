# TUI bar (kalin-bar-tui bar)

- **The bottom bar as a real terminal** — the endgame of the 2026-07-16 bar
  restyle session (typeset-statusline direction, B's bold type). Working
  end-to-end in nested sessions as of 2026-07-17; **not yet live** — the real
  shell keeps the QML BottomBar until panel-toggle parity (see cutover below).
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
- **Cutover checklist (open):**
  - P4 packaging: kitty + textual-image (+psutil) env + `kalin-bar-kitty`
    wrapper in home-config/desktop.nix; rebuild gate needs explicit approval.
  - Panel toggling from the bar (click a group → dockprep/dock the existing
    kalin_tuis panel foots; hover-grace later).
  - Losses at cutover to plan for: system tray (StatusNotifier pixmaps —
    drawable over TGP, work), MPRIS widget, calendar drawer, window peek,
    taskbar context menu.
  - Polish: focused-taskbar tint too subtle; battery meter's last cell
    rounds down at 100%.
  - Then: flip `useTuiBar` default, delete BottomBar + bar widgets (no dead
    code), re-sync this note + [[quickshell-shell]].
- Verified in nested sessions: strip reservation, dockprep/dock placement,
  respawn self-heal (killed kitty, BarHost brought it back), real icons,
  live meters/clock, env-flag opt-in against the real config dir.
