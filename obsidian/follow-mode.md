# Follow mode

Follow mode makes the [[viewport]] track windows automatically instead of being
moved by hand with [[pan]].

There are two independent toggles:
- **Follow focus** (`Super+Z`) — `viewport.follow`. The camera pans to keep the
  focused window centered.
- **Follow new windows** (`Super+Shift+Z`) — `viewport.follow_new_windows`. The
  camera pans to each newly spawned window.

Both follow states are reported over the [[ipc-socket]] so the
[[quickshell-shell]] can show whether follow is active.
