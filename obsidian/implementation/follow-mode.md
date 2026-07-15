# Follow mode

- Follow mode makes the [[viewport]] track windows automatically instead of being moved by hand with [[pan]].

- There are two independent toggles:
- **Follow focus** (`Super+Z`) — `viewport.follow`. The camera pans to keep the focused window centered.
- **Follow new windows** (`Super+Shift+Z`) — `viewport.follow_new_windows`. The camera pans to each newly spawned window.

- Both follow states are reported over the [[ipc-socket]] so the [[quickshell-shell]] can show whether follow is active.

- **Follow focus pans just enough to keep the window on screen — it no longer
  recenters on it (2026-07-09).** `viewport_follow_focus()` calls a
  dedicated `viewport_ensure_visible()` (`viewport_ops.c`), not
  `viewport_center_on()`: a no-op if the focused window is already fully
  visible, otherwise it moves the camera the minimum distance needed to
  bring the nearer out-of-view edge into frame (aligning to that edge, not
  the window's center). Reported as making every `Super+J`/`Super+K`/
  `Super+Arrow` focus change feel jumpy — cycling among on-screen windows
  used to jerk the whole camera to re-center each one in turn. Explicit
  "jump to this window" actions (the taskbar's fly-to-window,
  `viewport.follow_new_windows`'s auto-pan to a freshly spawned window) are
  unchanged and still call `viewport_center_on()` directly — this is a
  passive-follow-only fix, not a change to those deliberate jumps.

## Hover: instant focus switch, animated camera

- Moving the mouse over a different window switches keyboard focus
  **instantly** (`sloppyfocus` in `config.h`, on by default): `motionnotify()`
  calls `pointerfocus()`, which calls `focusclient()` the moment the pointer
  enters a new surface — no delay, no animation on the focus change itself.
- `focusclient()` unconditionally calls `viewport_follow_focus()` at the end,
  so every focus change (hover included) re-runs the pan-to-visible logic
  above. With `viewport.follow` and `viewport.smooth_pan` both on by default,
  the camera then glides to the newly-focused window
  (`viewport_move_to(nx, ny, 1)`'s smooth branch) while keyboard focus has
  already switched on the same event — focus is immediate, the camera is
  cosmetic follow-through, not a gate on it.
- If `viewport.follow` is off, hovering still switches focus instantly; the
  camera just doesn't move. `smooth_pan` off would make the camera snap
  instead of glide, but nothing in the codebase currently flips it at
  runtime, so in practice it always glides.
