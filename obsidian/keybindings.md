# Keybindings

Keybindings map keys to actions. They are defined at compile time in the `keys[]`
table in [[compile-time-config]] (`config.h` / `config.def.h`), in the dwm
tradition. `MODKEY` is Super. Only the first matched keybinding is executed.

Layout & focus:
- `Super+I` — `infinite` [[column-layout]] (default)
- `Super+F` — floating layout
- `Super+Space` — toggle between the two layouts
- `Super+Shift+Space` — toggle floating on the focused window
- `Super+Arrows` — [[directional-focus]]
- `Super+J` / `Super+K` — cycle focus through the window stack
- `Ctrl+Left` / `Ctrl+Right` — [[move-column]] (move focused window between columns)

Camera ([[viewport]]):
- `Super+Shift+Arrows` / `Super+Shift+HJKL` — [[pan]] left/right/up/down
- `Super+equal` / `Super+minus` — [[zoom]] in / out *(zoom is parked — see [[zoom]])*
- `Super+0` — [[fit-all]]
- `Super+BackSpace` — reset camera
- `Super+Z` / `Super+Shift+Z` — toggle [[follow-mode]] / follow-new-windows

Window sizing & appearance:
- `Super+[` / `Super+]` — narrow / widen focused window
- `Super+Shift+{` / `Super+Shift+}` — shorten / lengthen focused window
- `Super+D` / `Super+Shift+D` — dim / brighten focused window (per-window opacity)
- `Super+E` — toggle fullscreen
- `Super+Q` — close focused window
- `Super+C` — enter [[crop-mode]] (`Escape` or `Super+Shift+R` to cancel)

> The planned [[window-menu]] (a keybind-triggered [[quickshell-shell]] per-window
> action menu: resize, opacity, anchor, close, fullscreen) will sit **alongside**
> these binds, not replace them.

Monitors:
- `Super+comma` / `Super+period` — focus monitor left / right
- `Super+Shift+less` / `Super+Shift+greater` — move focused window to monitor left / right

Launching ([[spawn]]):
- `Super+T` — terminal (`foot`)
- `Super+P` — launcher (`fuzzel`)
- `Super+O` — toggle [[overview-mode]] via `qs ipc call windows-bar toggleOverview`

Session:
- `Super+Escape` — quit the compositor
- `Ctrl+Alt+Fn` — switch VT
- `Ctrl+Alt+Backspace` / `Terminate_Server` — quit

Legacy / vestigial (dwl heritage, candidates for removal):
- `Super+Return` — `zoom`, the dwm master-stack swap (moves the focused window to
  the top of the stack). Left over from dwl; being reviewed for removal.

See `code/config/config.h` for the authoritative table.
