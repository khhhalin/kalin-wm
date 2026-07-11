# Keybindings

- Keybindings map keys/chords/gestures to actions via a runtime DSL
  (`bind <chord> -> <action> [args]`), parsed from `code/config/default_binds.h`
  (shipped defaults) with a user override file at `~/.config/kalin-wm/binds.conf`
  (only written if missing — delete it and reboot to pick up a
  `default_binds.h` change during development).
- `MODKEY` is Super. Supports plain chords, `tap`/`hold` modifier-only binds,
  and pointer-button binds (`BTN_LEFT`/`BTN_RIGHT`).
- This replaces the old dwm-style compile-time `keys[]` table entirely.
- **Coverage is enforced (2026-07-11):** every action in the registry
  (`bind_actions.c`) must be covered by a `bind` line (any mode) or by
  `unbind <action-name>` — an explicit "I know about this action, I
  deliberately don't want it on a key" declaration — or `binds.conf` fails
  to load. At startup this is fatal (the compositor refuses to start; fix
  the file and relaunch); a live edit that fails this check is rejected with
  a logged error and the previous, still-working binds stay active (no
  crash mid-session). This exists because a config used to be able to
  silently drift out of sync with the compositor's own evolving action set
  — a real user's `binds.conf` had `Super+F` bound to an action from a
  since-removed feature and no bind at all for `Super+Shift+F`, both
  invisible until asked about directly; see the ledger entry for the full
  trace. `code/config/default_binds.h` is itself required to be fully
  covered too (enforced by `test_shipped_default_parses`), so a fresh
  install never boots with a silently-incomplete default either.

Window management ([[connection-graph]]):
- `Super+Arrows` — [[directional-focus]] (geometric cone search, unrelated to the connection graph)
- `Super+Ctrl+Arrows` — swap the focused window with its connection-graph neighbor in that direction
- `Super+J` / `Super+K` — cycle focus through the window stack
- `Super+Q` — close focused window (closing the middle of a line splices the two remaining neighbors together and closes the gap — see [[connection-graph]])
- `Super+[` / `Super+]` (`bracketleft`/`bracketright`) and `Super+equal`/`Super+minus` — narrow / widen focused window
- `Super+Shift+{` / `Super+Shift+}` (`braceleft`/`braceright`) and `Super+Shift+plus`/`Super+Shift+underscore` — shorten / lengthen focused window
- `Super+F` — fit width: stretch to the monitor's usable width, growing/shrinking evenly on both sides so the horizontal center stays put (does *not* reset world position — see the [[ledger]] for the bug where it used to)
- `Super+Shift+F` — fit height, same idea (vertical center stays put)
- `Super+M` — toggle maximized (fills `mon->w`, keeps border/bar, unlike fullscreen)
- `Super+E` — toggle fullscreen
- `Super+Shift+T` — toggle always-on-top
- `Super+Shift+O` — toggle overlap (let the focused window overlap its connection-graph neighbors instead of pushing them)
- `Super+L` — link-pick: arm the focused window as a pending connection source; the next click on another window links them (see [[connection-graph]])
- `Super+N` — toggle minimized
- `Super+D` / `Super+Shift+D` — dim / brighten focused window (per-window opacity)
- `Super+grave` — toggle scratchpad `foot --app-id=kalin-scratchpad`
- `Super+BTN_LEFT` (drag) — move window (drags its whole connected component — see [[connection-graph]])
- `Super+BTN_RIGHT` / `Super+Ctrl+BTN_RIGHT` (drag) — resize window

Camera ([[viewport]]):
- `Super+Shift+Arrows` / `Super+Shift+HJKL` — [[pan]]
- `Super+Ctrl+equal` / `Super+Ctrl+minus` — [[zoom]] in / out *(zoom is parked — see [[zoom]])*
- `Super+0` — [[fit-all]] (`viewport.fit`)
- `Super+BackSpace` — reset camera (`viewport.reset`)
- `Super+Z` / `Super+Shift+Z` — toggle [[follow-mode]] / follow-new-windows
- `Super+Ctrl+BTN_LEFT` (drag on empty canvas) — direct-manipulation camera pan

Monitors:
- `Super+comma` / `Super+period` — focus monitor left / right
- `Super+Shift+less` / `Super+Shift+greater` — move focused window to monitor left / right

Launching ([[spawn]]):
- `Super+T` — terminal (`foot`)
- `Super+P` — launcher (`fuzzel`)
- `Super+O` — toggle [[overview-mode]] (native compositor zoom-out, not shell-rendered)
- `tap Super` — toggle launcher (`fuzzel`)
- `hold Super` — [[window-menu]]
- `Super+Print` — screenshot (whole focused monitor, immediate)
- `Super+Shift+S` — niri-style interactive screenshot UI (see [[screenshot-ui]]): opens with the whole monitor pre-selected, drag to draw a custom region, Escape cancels, Space/Enter confirms (disk + clipboard), Ctrl+C confirms clipboard-only, P toggles pointer visibility
- `Super+V` — clipboard history: `foot -e kalin-clip-picker`, an fzf TUI over `cliphist` (the picker script itself lives in `home-config/desktop.nix`, not this repo — kalin-wm only owns the keybind)

Session:
- `Super+Escape` — quit the compositor
- `Ctrl+Alt+Terminate_Server` — quit
- `Ctrl+Alt+Fn` — switch VT

See `code/config/default_binds.h` for the authoritative, exact table (this
note can drift — check there first if something doesn't match).
