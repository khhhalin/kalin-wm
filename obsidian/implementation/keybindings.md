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

Window management:
- `Super+Arrows` — [[directional-focus]] (geometric cone search)
- `Super+Ctrl+Arrows` — **unbound as of 2026-08-13** (was swap-with-neighbor; the
  [[connection-graph]] it drove was removed — [[layout-impl]]). Reserved for the
  Phase 2 rail 1D-swap.
- `Super+J` / `Super+K` — cycle focus through the window stack
- `Super+Q` — close focused window (as of 2026-08-13 just leaves a hole where it
  was; gap-close returns rail-based in Phase 2 — [[layout-impl]])
- `Super+[` / `Super+]` (`bracketleft`/`bracketright`) and `Super+equal`/`Super+minus` — narrow / widen focused window
- `Super+Shift+{` / `Super+Shift+}` (`braceleft`/`braceright`) and `Super+Shift+plus`/`Super+Shift+underscore` — shorten / lengthen focused window
- `Super+F` — fit width: stretch to the monitor's usable width, growing/shrinking evenly on both sides so the horizontal center stays put (does *not* reset world position — see the [[ledger]] for the bug where it used to). Also re-centers the camera horizontally on the window (`viewport_center_on_x()`), leaving vertical pan untouched.
- `Super+Shift+F` — fit height, same idea (vertical center stays put, and the camera re-centers vertically only — `viewport_center_on_y()`)
- `Super+M` — toggle maximized (fills `mon->w`, keeps border/bar, unlike fullscreen)
- `Super+E` — toggle fullscreen
- `Super+Shift+T` — toggle always-on-top
- `Super+Shift+O` — toggle overlap flag. **Dormant** as of 2026-08-13: its
  consumer (`resolve_growth_overlap`) went with the [[connection-graph]]; the flag
  still toggles + broadcasts but does nothing until the Phase 3 rail grow-push
  ([[layout-impl]]).
- `Super+L` — **unbound as of 2026-08-13** (was link-pick / manual connect; removed
  with the [[connection-graph]]). Reserved for the Phase 5 overlay-pin.
- `Super+N` — toggle minimized
- `Super+I` — toggle paper mode: composite the focused window through `shaders/paper.frag` (warm reading tint) via the [[shaders|paper-shader-core]] per-window API. Flag lives on `Client.paper_mode`; driven per-frame by `client_apply_paper()` in `rendermon()`, which reinjects a shaded overlay above the surface. Toggling on seeds `Client.paper_yellow` to `paper_yellow_default` (0.7). Also settable per-appid with a `paper` field on a `rules[]` entry (`applyrules()`). Uniform defaults (`paper_strength`/`paper_color`/`paper_ink`/`paper_preserve` — the yellow=1 endpoint — plus `paper_yellow_default`/`paper_aged`) are in `config.def.h`. GPU-verified working on GLES2 (see [[shaders]]).
- `Super+Y` / `Super+Shift+Y` — ramp the focused window's papyrus knob (`Client.paper_yellow`, 0..1) by ±0.1 (`paper-yellow` / `ACT_PAPER_YELLOW`, repeatable). 0 = crisp warm page, 1 = aged saturated tan; `client_apply_paper()` maps it through `ws_paper_from_yellow()` (eased strength + page warming). Ramping to 0 turns paper mode off. Broadcast to the shell as `focused.yellow` for the `WindowActions` papyrus gauge.
- `Super+D` / `Super+Shift+D` — dim / brighten focused window (per-window opacity)
- `Super+grave` — toggle scratchpad `foot --app-id=kalin-scratchpad`
- `Super+BTN_LEFT` (drag) — move window. As of 2026-08-13 moves **only the grabbed
  window** (group-drag went with the [[connection-graph]] removal — [[layout-impl]]).
- `Super+Ctrl+BTN_LEFT` (drag on a window) — same single-window move as `Super+BTN_LEFT`
  now (the two modes `CurMove`/`CurMoveSolo` differ only in the empty-canvas
  fallback). On empty canvas this is camera pan instead — see below.
- `Super+BTN_RIGHT` / `Super+Ctrl+BTN_RIGHT` (drag) — resize window, grabbing whichever corner is nearest the cursor (not always bottom-right); the opposite corner stays anchored

Camera ([[viewport]]):
- `Super+Shift+Arrows` / `Super+Shift+HJKL` — [[pan]]
- `Super+ScrollUp/Down/Left/Right` (two-finger touchpad scroll or wheel while
  Super held) — [[pan]]; the touchpad replacement for the 3-finger swipe that
  [[gestures|semi-MT pads]] can never emit. Finger deltas accumulate to one
  dispatch per `scroll_bind_step` of travel (see [[scrolling]]).
- `Super+Ctrl+equal` / `Super+Ctrl+minus` — [[zoom]] in / out *(zoom is parked — see [[zoom]])*
- `Super+0` — [[fit-all]] (`viewport.fit`)
- `Super+BackSpace` — reset camera (`viewport.reset`)
- `Super+Z` / `Super+Shift+Z` — toggle [[follow-mode]] / follow-new-windows
- `Super+Ctrl+BTN_LEFT` (drag on empty canvas) — direct-manipulation camera pan; on a window this is solo move instead (see Window management above)

Monitors:
- `Super+comma` / `Super+period` — focus monitor left / right
- `Super+Shift+less` / `Super+Shift+greater` — send focused window to monitor left / right ([[multi-camera]] Phase 2, `tagmon()`): teleports it to the center of what that monitor's camera currently shows; camera-bypassed docked/fullscreen/maximized windows just switch holder without the teleport (the cross-camera edge-severing this used to also do went with the [[connection-graph]] removal 2026-08-13)

Launching ([[spawn]]): the app commands are the kalin-tools wrapper family
(`~/environment/kalin-tools` — one persistent tmux session per app, viewed
through a dedicated foot window; raise-or-spawn via the IPC clients feed).
- `Super+T` — terminal-browser tab (`kalin-term-tab`: new tab in the shared
  "terminals" session + raise the viewer; vault drift fixed 2026-07-25 — this
  stopped being plain `foot` when the tab model landed)
- `Super+Ctrl+T` — tab picker (`kalin-term-pick`, fuzzel over tabs)
- `Super+Space` — bare `foot` (no tmux)
- `Super+A` — Claude window (`kalin-claude`: "deck" session, claude-tui picker; added 2026-07-25)
- `Super+H` — editor window (`kalin-hx`: "helix" session, single hx instance)
- `Super+P` — launcher (`foot --app-id=kalin-launcher -e kalin-launch`; see [[app-launcher]])
- `Super+O` — toggle [[overview-mode]] (native compositor zoom-out, not shell-rendered)
- `tap Super` — toggle launcher (same `kalin-launch`; tracked via the `kalin-apps` tmux window "launcher", killed to toggle off)
- `hold Super` — [[window-menu]]
- `Super+Print` — screenshot (whole focused monitor, immediate)
- `Super+Shift+S` — niri-style interactive screenshot UI (see [[screenshot-ui]]): opens with the whole monitor pre-selected, drag to draw a custom region, Escape cancels, Space/Enter confirms (disk + clipboard), Ctrl+C confirms clipboard-only, P toggles pointer visibility
- `Super+V` — clipboard history: `foot -e kalin-clip-picker`, an fzf TUI over `cliphist` (the picker script lives in `~/environment/kalin-tools`, nix only wraps it — kalin-wm only owns the keybind)

Session:
- `Super+Escape` — quit the compositor
- `Ctrl+Alt+Terminate_Server` — quit
- `Ctrl+Alt+Fn` — switch VT

See `code/config/default_binds.h` for the authoritative, exact table (this
note can drift — check there first if something doesn't match).
