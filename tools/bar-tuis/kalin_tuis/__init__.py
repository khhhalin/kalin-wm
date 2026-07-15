"""kalin_tuis — the docked bar panel TUIs for kalin-wm's Quickshell bar.

One Textual app per panel (wifi/bluetooth/mixer/stats/clipboard/battery/
display), all sharing theme.py's palette and app.py's scaffold so they read
as one system. Entry point is `python3 -m kalin_tuis <panel>` (the
`kalin-bar-tui` wrapper in home-config/desktop.nix); each app runs inside a
foot terminal that DockedPanel.qml docks into the bar. See
obsidian/bar-tuis.md for the design map.
"""
