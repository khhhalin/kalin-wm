# Quickshell shell

- The Quickshell shell is kalin-wm's companion desktop shell, written in QML on Quickshell 0.3.0.
- It lives in `~/environment/quickshell` (a plain directory, not part of the kalin-wm git repo).
- It provides the bottom bar, [[overview-mode]], window peek, notifications, OSD, and side panels.

- The shell talks to kalin-wm through two channels: the [[foreign-toplevel]] protocol (window list and control) and the [[ipc-socket]] ([[viewport]] camera state and commands).
- Its `CompositorService` picks the kalin-wm backend when `$KALIN_IPC_SOCKET` is set, otherwise a niri backend.

- The shell is now fully ported to kalin-wm:
- `Overview.qml` and `WindowPeek.qml` use `CompositorService`.
- `TaskbarService.qml` now consumes `CompositorService.windows` and uses its
  unified `activate()` / `close()` actions, so pinned/running app buttons work
  on both niri and kalin-wm.
- `WorkspaceIndicator.qml` is hidden on kalin-wm because the infinite-canvas
  model has no fixed workspaces.

- A **Display** tab in the right system panel lets the user view and reorder outputs left-to-right under niri.
- It uses a new `DisplayService` singleton that polls `niri msg --json outputs` and applies positions via `niri msg output <name> position set <x> <y>`.
- On kalin-wm the tab is disabled with a placeholder.

## Known crash class: null captureSource kills the whole Wayland connection

- `Overview.qml` and `WindowPeek.qml` each poll a `ScreencopyView` thumbnail on
  a periodic `Timer` (`OverviewState.thumbnailRefreshMs`, see that note for
  why it's periodic rather than `live: true`). If the window a thumbnail is
  showing closes right as the timer ticks, `captureSource` can already be
  null by the time `captureFrame()` runs — the toplevel-close event and the
  timer tick aren't synchronized.
- Calling `captureFrame()` on a null `captureSource` sends a
  `hyprland_toplevel_export_manager_v1.capture_toplevel_with_wlr_toplevel_handle`
  request with a null handle. That's a **non-nullable protocol argument** —
  libwayland's client-side marshalling rejects it locally and the whole
  Wayland connection dies (`The Wayland connection experienced a fatal
  error: Invalid argument`), not just that one thumbnail. Not QML-catchable;
  the entire shell goes down and has to reconnect from scratch.
- **First fix attempt (2026-07-09, insufficient on its own)**: guarded both
  `Timer.onTriggered` handlers with `if (thumbView.captureSource)
  thumbView.captureFrame()`. Build/tests were clean, but **this recurred live
  on real hardware** with the identical crash trace — the guard only covered
  `captureFrame()` calls; it turned out the crash also fires from simply
  *assigning* `captureSource` to null on a still-alive `ScreencopyView`
  (`captureSource: modelData.toplevel` is a live binding that reassigns to
  null the instant a window closes). Quickshell's own compiled
  capture-negotiation code appears to react to that property reassignment by
  attempting `capture_toplevel_with_wlr_toplevel_handle` with the new (null)
  value directly, independent of any `captureFrame()` call — a path no QML
  guard on the timer alone can intercept.
- **Actual fix (2026-07-09)**: wrap each `ScreencopyView` in a `Loader` keyed
  on the handle's validity (`active: modelData.toplevel !== null` /
  `active: modelData !== null`), and snapshot the capture source *once* at
  creation time via `Component.onCompleted: pinnedSource = ...` into a plain
  property, rather than binding `captureSource` directly to the (potentially
  null-going) model value. When the window closes, the `Loader` destroys the
  whole `ScreencopyView` instance instead of reassigning its `captureSource`
  property — so a live instance's `captureSource` is now never once set to
  null. Confirmed no QML syntax errors via `qmllint` (some unrelated
  pre-existing "unqualified access" warnings remain, expected since qmllint
  can't resolve Quickshell's own singletons/types standalone).
- **Second fix attempt also recurred live**, byte-identical trace, closing
  windows in general — not obviously while Overview/peek was even open.
  That was the tell: **`Overview {}` (`shell.qml`) and `WindowPeek {}`
  (`WindowsBarScreen.qml`) are both instantiated directly, not behind a
  visibility-gated `Loader`.** A `PanelWindow`'s `visible: false` only hides
  the panel — it does not tear down the component tree underneath. That
  means every open window's `ScreencopyView`/`Timer` inside `Overview.qml`
  was alive and firing `captureFrame()` every `thumbnailRefreshMs`
  *continuously in the background*, regardless of whether the user ever
  opened Overview — and `WindowPeek.qml`'s `root.appId` can stay set to the
  last-hovered app after the popup closes (only `root.show` flips), keeping
  that app's thumbnails capturing indefinitely too. This is why the crash
  reproduced on *every* window close, not just while looking at a thumbnail
  grid.
- **Third fix (2026-07-09)**: both files' `Loader.active` now also requires
  `OverviewState.visible` / `root.show`, so the `ScreencopyView` (and its
  Timer) is only constructed while the user can actually see it — combined
  with the earlier pinnedSource freeze, capture now only exists, and can
  only race a window-close, during the narrow window the thumbnail is
  actually on screen, instead of for the entire lifetime of every open
  window.
- Checked `qmllint` again — no syntax errors on either file after this
  change.
- **Still not re-verified live** — needs another real-hardware run. Given
  two prior fixes each looked complete and failed anyway, don't treat this
  as closed until it's actually been exercised (close windows both with
  Overview/peek open *and* closed) without a repeat crash.
- Fix: guard both `onTriggered` handlers with
  `if (thumbView.captureSource) thumbView.captureFrame()` (and the matching
  `peekView` one in `WindowPeek.qml`, which additionally lacked the
  null-toplevel `visible` guard `Overview.qml` already had).
- Found by reading `/tmp/kalinwm.log` (the `kalinwm` dev launcher's `tee`'d
  output — see [[test-vm]]/AGENTS.md; this is what to check first for a
  real shell crash, not the systemd journal, since a `ly`-started session
  doesn't capture quickshell's stdout anywhere).

## Super+Q cannot close the bar

- `Super+q` is bound to `close` (`killclient()`, `dwl.c`), which operates on
  the focused *Client* (an xdg-toplevel window) — kalin.wm's normal keyboard
  focus stack. The quickshell bar is a `LayerSurface` (layer-shell), a
  completely separate type that never enters that focus stack, so `close`
  has no path to it. Confirmed by reading `killclient()`/`ACT_CLOSE` — this
  isn't a guess. If the bar disappears, Super+Q is not the mechanism; look
  for a real client-side crash/disconnect (see the crash class above, and
  the new layer-shell destroy/unmap logging below) instead.

## No compositor-side log on layer-surface destroy/unmap (fixed 2026-07-09)

- Before this, `destroylayersurfacenotify()`/`unmaplayersurfacenotify()`
  (`dwl.c`) were completely silent — if the bar disappeared, kalin-wm's own
  log had zero trace of it, only whatever the client's own stdout happened
  to capture (and per the note above, a `ly`-started real session captures
  none of that at all). Added `wlr_log(WLR_INFO, ...)` to both, logging the
  layer surface's `namespace` and `layer` — so a future disappearance is at
  least visible from the compositor side even when nothing captured the
  client's own stdout.

## Docked TUI panels: DockedPanel.qml (2026-07-10, generalized same day)

Originally built as a one-off for the clipboard panel (bar button +
`ClipboardPanelState.qml` singleton), then generalized the same day into a
reusable `modules/DockedPanel.qml` once the same pattern was needed for
stats/wifi/bluetooth/volume/display too — `ClipboardPanelState.qml` is
**deleted**, fully absorbed into the generic component. Every right-side
panel that launches a real TUI app (as opposed to the QML-rendered
Battery pane, still on the old `SidePanel`/`rightOwner` system) is now one
`DockedPanel { appId; command; screen; barHeight }` instance in
`BottomBar.qml`, paired with its bar button right next to it:

| Button | appId | command |
|---|---|---|
| `SystemStatsWidget` | `kalin-stats-panel` | `kalin-bar-tui stats` |
| `WifiLauncher` | `kalin-wifi-panel` | `kalin-bar-tui wifi` |
| `BluetoothLauncher` | `kalin-bt-panel` | `kalin-bar-tui bluetooth` |
| `BatteryWidget` | `kalin-battery-panel` | `kalin-bar-tui battery` |
| `VolumeWidget` | `kalin-volume-panel` | `kalin-bar-tui mixer` |
| `DisplayWidget` | `kalin-display-panel` | `kalin-bar-tui display` |
| `ClipboardButton` | `kalin-clip-panel` | `kalin-bar-tui clipboard` |

As of 2026-07-15 every panel runs a custom Textual TUI from the
[[bar-tuis]] suite (`tools/bar-tuis/` in this repo, one `kalin-bar-tui
<panel>` dispatcher packaged in home-config/desktop.nix) — btop, nmtui,
bluetuith, wiremix, and the fzf clip-picker loop are no longer used by the
bar. Battery joined the docked system at the same time: `SystemPanel.qml`
is deleted and the `SidePanel`/`rightOwner` drawer now serves only the
clock's calendar (`SystemPanelState.rightOwner` is `"clock" | ""`).

`KalinViewport.dock()/undock()/minimize()` (functions wrapping the kalin-wm
IPC commands of the same names — see [[ipc-socket]]) do the actual
positioning. Deliberately **not** the `SidePanel` drawer/`Component` system
(`rightOwner`, `CalendarPanel`-style) still used by the clock/battery tabs —
that system loads *QML content* into a shared drawer, but a docked panel's
content is a real Wayland toplevel the compositor positions, needing its own
independent open/close state and process lifecycle. Hover-to-open (via the
bar button's `hovered` or the compositor's `dock_hover` IPC field once the
cursor is over the real toplevel) plus click-to-pin match the drawer's own
UX; a `DockedPanelCoordinator.qml` singleton enforces that only one docked
panel is ever open **per monitor** at a time, since panels on the same
screen share that screen's on-screen rect (claim/closeRequested, keyed by
screen name since a `WindowsBar.qml` instance — and its DockedPanels —
exists per connected monitor as of 2026-07-11, not just the laptop panel;
see the ledger entry for both the coordinator rescoping and why each
panel's `appId` also became `<name>-<screen.name>`, e.g.
`kalin-stats-panel-DP-3`, so two monitors don't fight over the same
compositor-side client).

`BarConfig.tuiPanelWidth`/`tuiPanelHeight` (700×480) size these panels — a
separate pair from `panelWidth`/`panelHeight` (440×520, sized for QML drawer
content), because `btop` refuses to render below 80×24 terminal cells and
440px only fit ~51 columns at foot's default font.

First click/hover spawns the command once (`DockedPanel.spawned` gates
this); every later toggle just re-docks/minimizes the same already-running
client, so reopening is instant with the session exactly as it was left —
never a respawn, never a dead shell prompt.

- **Real bug hit and fixed during the original clipboard build**: `Process { command: [...] }`
  set *declaratively* inside the `Process` block silently produced a bare
  `foot` with **no arguments at all** (confirmed via `/proc/<pid>/cmdline`
  showing only `foot`, no `--app-id`/`-e`) — foot then fell back to spawning
  the user's default shell instead of `kalin-clip-picker-loop`. Every other
  `Process` example in this codebase (`CalendarService.qml`,
  `VolumeWidget.qml`, etc.) sets `.command` **imperatively**, right before
  `.running = true`, never inside the `Process {}` block itself. Switching
  to that pattern fixed it immediately. Lesson: match the codebase's
  existing `Process` usage exactly — the declarative form isn't just a
  style preference, it doesn't reliably thread argv.
- First spawn needs a ~500ms beat (`firstSpawnDelay` Timer) before
  dock/minimize by app_id can find the new client — kalin-wm's client list
  doesn't have it yet immediately after `running = true`; addressing by
  app_id silently no-ops on a miss (same race documented in the docking
  primitive's own ledger entry). Only the first open pays this cost.
- Verified in the [[test-vm]] (which needed its own VM-local stand-in
  `kalin-clip-picker`/`-loop` packages added since `home-config` isn't a
  flake input there — see `test-vm/vm.nix`): full open → close → reopen
  cycle confirmed via the guest's own `ps -ef`, not host-side `ps` (tripped
  over this myself mid-session — `ps aux` in the host shell only ever shows
  host processes, checking VM state requires typing the check into a guest
  terminal via `vmctl`). Open spawns the real process tree (`foot` → `bash`
  → `kalin-clip-picker-loop` → `kalin-clip-picker` → `fzf`) and docks it
  (borderless, positioned bottom-right, confirmed via screenshot); close
  hides it (confirmed still in the process list, just not visible); reopen
  shows the same session instantly. Also hit and worked around a
  VM-input-harness quirk: the very first synthetic click after a fresh boot
  needs a preceding real click somewhere else first or it's silently
  dropped — not a shell/compositor bug, an artifact of the QMP-injected
  test input.
