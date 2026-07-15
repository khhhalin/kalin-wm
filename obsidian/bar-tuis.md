# bar-tuis

- The custom Textual (Python) TUI suite behind every docked bar panel of the [[quickshell-shell]] — wifi, bluetooth, mixer, stats, clipboard, battery, display.
- Source: `tools/bar-tuis/kalin_tuis/` in this repo. One dispatcher entry point: `python3 -m kalin_tuis <panel>`, packaged as `kalin-bar-tui` (home-config/desktop.nix `barTuis`, PATH-prefixed with its backend CLIs; `kalin-display-panel` remains as a one-line alias). test-vm/vm.nix carries a parallel `testBarTuis` wrapper.
- Replaced (2026-07-15): btop, nmtui, bluetuith, wiremix, `kalin-clip-picker-loop` (the loop existed only because fzf exits on selection — the clipboard TUI copies and stays alive; the one-shot `kalin-clip-picker` remains for the `Super+V` keybind), and the QML SidePanel battery pane (`SystemPanel.qml`, deleted).

## Shared design

- `theme.py` — the single source of truth for the palette. Every hex is copied from `foot.ini` (dotfiles) or the bar's `Theme.qml`; a re-rice there must be mirrored here. **`background` must stay exactly foot's `#1e1915`**: foot's `alpha-mode=matching` makes only bg-matching cells translucent — one digit off and panels become opaque slabs. Hardcoded hex over `ansi_color=True` because ANSI passthrough caps Textual at 16 colors and foot's palette lacks the bar's amber `#f0a030`.
- `app.py` — `KalinPanelApp` scaffold: Header (no clock, icon hidden, command palette disabled) / one-line `#status-line` / panel content / Footer with bindings. `q`/`ctrl+c` quit everywhere (safe: DockedPanel respawns on exit), `r` refresh.
- `widgets.py` — `Gauge` (label + bar + right-aligned %). `util.py` — `run()`/`run_async()` subprocess helpers + the kalin-IPC client (moved out of the old display panel).
- App CSS is `SHARED_CSS + "..."` per panel — `App.CSS` is not MRO-merged like a widget's `DEFAULT_CSS`.
- Textual gotcha: a focused `OptionList` consumes `enter`, so app-level enter bindings are footer-display only — the live path is `on_option_list_option_selected`. Priority-binding enter at App level would steal it from modal `Input`s.

## Backends per panel

| Panel | Backend | Updates |
|---|---|---|
| wifi | `nmcli -t` (parse with `split_terse` — `\:` escaping) | poll 15s `--rescan no`; explicit rescan on `s`; `run_async` for connect |
| bluetooth | dbus-fast → BlueZ ObjectManager + `Agent1` (KeyboardDisplay) registered as default agent | 3s GetManagedObjects poll + InterfacesAdded/Removed signals |
| mixer | `pw-dump` for the node list/defaults; **volume/mute via `wpctl get-volume <id>` per node** | poll 2s, render on diff |
| stats | psutil + GPU readers ported from the bar's `SystemStats.py` (probed once) | poll 2s |
| clipboard | `cliphist list/decode/delete` + `wl-copy` (bytes end-to-end for images) | poll 5s, hash-diff |
| battery | dbus-fast → UPower DisplayDevice `PropertiesChanged` (+ statics enriched once from the real `battery_BATn` device — the aggregate lacks Model/EnergyFullDesign) + `powerprofilesctl` | events + 30s safety poll |
| display | kalin IPC / niri, unchanged from the old `tools/display-panel/` | poll 2s |

- **Mixer hard lesson**: a device node's own `Props.channelVolumes`/`mute` in `pw-dump` can disagree with reality — WirePlumber keeps ALSA device volume/mute on the *device route*. `wpctl get-volume` is the ground truth (and its value is already cubic-scaled; no cbrt).

## Docking lifecycle fixes that landed with the suite

- `DockedPanel.qml`: `Process.onExited` resets `spawned` (a quit/crashed TUI used to leave a dead bar button for the whole shell session); `firstSpawnDelay` only shows if still open; `lateSpawnSettle` re-asserts undock+minimize for 60s after first spawn — a client that maps *after* the panel logically closed used to appear docked with nothing left to hide it (cold VM spawns take 60s+; host is sub-second).
- `ipc.c`: broadcast writes now preserve line framing across short writes (per-client `resync` flag leads the next send with `\n`) and `ipc_build_state` restores the trailing `\n` on truncation — a partial write used to desync the shell's whole line stream (seen as `KalinViewport: bad state line` warnings and panels stuck open on stale `dock_hover`). A truncated record is still *lost* (one bad parse, self-healing); eliminating loss entirely would need per-client output buffering — open follow-up if stale-state symptoms persist.
- `ipc.c` (second, bigger cause of the same warnings): the modes/outputs/conns builders left snprintf's *partial entry bytes* in the buffer when they ran out of room, and the final `%s` emitted them — with an external monitor's long mode list this corrupted **every** state line. NUL restored at each break site; buffers sized for two monitors' full mode lists (IPC_BUF_SIZE 16384).
- `BottomBar.qml`/`SidePanel.qml`: removed `required property ShellScreen screen` — redeclaring it shadows PanelWindow's real `screen` property, which silently sent every monitor's bar to the default output. Never redeclare `screen` on a window-rooted component.

## Dev loop

- Standalone: `nix-shell -p 'python3.withPackages (ps: [ps.textual ps.psutil ps.dbus-fast])'`, then `python3 -m kalin_tuis <panel>` from `tools/bar-tuis/` (PYTHONPATH or cwd).
- Docked without a rebuild: put a `kalin-bar-tui` shim (exec the nix-shell python with `PYTHONPATH=tools/bar-tuis`) on PATH and relaunch `qs` with that PATH — the QML already invokes `kalin-bar-tui`, so no config edits needed.
- VM: `nix build .#vm --override-input quickshell-config path:$HOME/environment/quickshell` to test uncommitted QML (the pinned input is the GitHub repo). Bluetooth/battery show graceful "unavailable" states there; wifi lists 0 networks (no radio).
