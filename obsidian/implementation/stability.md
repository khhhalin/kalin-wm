# Stability

- Stability is the primary blocker for kalin-wm v1.0.
- A stability audit identified 23 tracked issues (4 critical, 8 high, 9 medium, 2 low), all in the Phase 0 checklist, and all now fixed and re-verified against the live code.

- The most critical areas were: [[crop-mode]] (division by zero, NULL derefs, double-free), input handling (NULL keyboard state, `grabc` deref during drag), layout calculation (division by zero, infinite recursion risk), and client lifecycle (memory leaks, unchecked allocations).
- A separate [[spawn]] crash was also fixed.

- The project's coding rule is defensive C: every pointer deref NULL-checked, every divisor non-zero, every allocation failure handled.
- The full findings are in [[research/active-design/stability-audit|the stability audit]]; fixes are summarized in [[research/active-design/fixes-summary|the fixes summary]].

## Compositor crash: toplevel_export CPU-readback buffer overflow (2026-07-08)

- Real-hardware segfault, root-caused via `coredumpctl` + gdb backtrace (no
  crash log existed to point at instead — the same `mkdir()`-not-recursive
  bug documented in [[persistence]] was also silently disabling
  `crash_report.c`'s own crash-log directory, fixed the same way once found).
- `code/src/modules/protocols/toplevel_export.c`'s `render_client_to_buffer()`
  CPU-readback path (used by the [[quickshell-shell]]'s window-preview
  protocol, `hyprland-toplevel-export-v1` — see [[protocols]]) called
  `wlr_texture_read_pixels()` with an empty `src_box` (= "read the whole
  texture," per its own doc comment) into a client-supplied destination
  buffer, with no check that the buffer was actually sized for the window's
  *current* dimensions. Quickshell's destination buffer can be sized for a
  window's *previous* size if the window was resized (most easily triggered
  by `Super+F`/`fitwidth()` actually working correctly — see the [[ledger]])
  between Quickshell requesting the buffer and the frame arriving: a real
  heap buffer overflow, not hypothetical.
- Fix: compare `dst_buffer->width/height` against `c->geom.width/height`
  before copying; refuse (log + return) instead of overflowing on a
  mismatch. Quickshell re-requests with a correctly-sized buffer once it
  catches up.

## Quickshell bar: chronic crash-loop, root cause was buffer-negotiation churn, not the memory volume it looked like (2026-07-09)

- Symptom: the [[quickshell-shell]] bar crashes repeatedly and silently
  relaunches (`qs::launch::checkCrashRelaunch` — Quickshell has its own
  crash-detecting relaunch wrapper, so from the user's seat it reads as "the
  bar glitches/resets," not an obvious crash). Confirmed via
  `/run/user/1000/quickshell/by-id/*/log.log`: 30+ distinct instance
  directories accumulated over 2026-07-07 through 07-09, some spawned only
  seconds apart.
- Two separate things were found, investigated with `coredumpctl` (needed
  `nix-shell -p gdb` — not installed by default) against real crash
  coredumps:
  1. **Already fixed before this investigation, confirmed stable since:** a
     Qt/QML delegate-model crash (`VDMListDelegateDataType::
     createMissingProperties`, consistent identical stack across multiple
     independent coredumps) from `Repeater.setModel()` racing with in-flight
     delegate incubation — triggered by reassigning a *new* plain JS array to
     a Repeater's `model:` on every incoming notification.
     `NotificationService.qml` already uses a `ListModel` with
     `insert()`/`remove()` instead (comment in that file names the exact
     crash signature) — no coredump with this signature after that fix
     landed (2026-07-06).
  2. **Found and fixed in this pass:** `Overview.qml` and `WindowPeek.qml`
     both bound every visible thumbnail tile's `ScreencopyView.live` to
     `true` continuously — with several windows open, that's many
     concurrent *per-compositor-frame* buffer-negotiation attempts, and on
     this hardware/driver combination that dmabuf negotiation reliably fails
     (`Unable to create dmabuf for request: No matching formats`, falling
     back to SHM, every single attempt, forever, for as long as the overlay
     stays open) — a continuous allocate-fail-fallback churn, not a one-time
     cost. `ScreencopyView` exposes no frame-rate throttle, only binary
     `live` plus a one-shot `captureFrame()` method
     (`quickshell-wayland-screencopy.qmltypes`). Fixed by turning `live` off
     and driving `captureFrame()` from a `Timer` instead
     (`OverviewState.thumbnailRefreshMs`, default 2000ms) — cuts the
     negotiation rate by roughly two orders of magnitude for a handful of
     open windows. The underlying dmabuf-format mismatch itself is not
     fixed (unclear whether it's fixable from either side — Quickshell's
     buffer allocator is closed to us, prebuilt package; kalin-wm's
     `toplevel_export.c` already had to special-case Quickshell's real
     buffer-submission behavior once, see above) but it no longer runs
     continuously, so its cost is bounded.
- **What "the memory problem" actually was:** not a leak of raw bytes so
  much as an unbounded *rate* of failed allocation attempts scaling with
  (open windows) × (compositor frame rate) for as long as a preview overlay
  stayed open — the fix is a throttle, not a leak plug. Worth re-visiting if
  crashes persist: check `coredumpctl list | grep quickshell` for a new
  signature after this fix, since the two causes found here may not be the
  only ones.
