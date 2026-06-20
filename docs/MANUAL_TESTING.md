# Manual Testing Checklist (kalin-wm)

This checklist is focused on the current known-risk areas: spawn stability, infinite layout placement, viewport pan/zoom, crop mode, and input/focus behavior.

## 1) Build + baseline checks

1. Build:
   - `nix develop -c make clean all`
2. Run unit tests:
   - `nix develop -c make test-unit`
3. Optional coverage run:
   - `nix develop -c make test-unit-coverage`

Expected: build succeeds and unit tests pass.

---

## 2) Start compositor for manual tests

### Option A (recommended): nested run with helper
- Run: `./scripts/dev/run-nested-safe.sh`
- Choose `A` if your key remapper (kanata) interferes.

### Option B: TTY run
- Run: `./scripts/run-tty`

For logs:
- Nested log: `/tmp/nested.log`
- Crash/debug log helper: `./scripts/dev/run-with-logging.sh`

---

## 3) Infinite layout + spawn placement

Goal: verify windows do not overlap on spawn and extend in column flow.

1. Spawn first terminal.
2. Spawn second and third terminals.
3. Keep spawning up to 6–8 windows.
4. Observe placement:
   - New windows should not stack directly on top of previous ones.
   - Placement should continue in the layout flow (column/strip behavior).

Expected: no overlap-on-spawn regression.

---

## 4) Scroll/pan/zoom behavior

Goal: verify camera movement updates already-mapped windows correctly.

1. With multiple windows open, pan viewport in all directions.
2. Zoom in and zoom out repeatedly.
3. Reset viewport.
4. Pan after zoom (to catch transform ordering issues).

Expected:
- Panning visibly moves viewpoint over the workspace.
- Zoom changes are applied consistently.
- No sudden client pile-up/overlap after pan/zoom.

---

## 5) Focus + follow mode

1. Focus different windows using your keybindings.
2. Toggle follow mode on/off.
3. Change focus again.

Expected:
- Focus switches cleanly.
- When follow is enabled, camera recenters/follows focused client behavior.
- When follow is disabled, no auto recenter.

---

## 6) Crop mode

1. Focus a window.
2. Enter crop mode.
3. Drag a selection and release.
4. Repeat with tiny and large selections.
5. Enter crop mode and cancel.

Expected:
- Crop UI appears and updates during drag.
- Crop apply resizes/crops target window without crash.
- Cancel always exits crop mode cleanly.

---

## 7) Stability stress pass (quick)

1. Open 10+ windows quickly.
2. Alternate focus while spawning new windows.
3. Pan + zoom during this process.
4. Close windows rapidly.

Expected:
- No freeze/crash.
- Input remains responsive.
- Layout remains coherent.

---

## 8) Failure capture

If something breaks:
1. Re-run with logging: `./scripts/dev/run-with-logging.sh`
2. Save relevant tail:
   - `tail -200 /tmp/kalin-crash.log`
   - `tail -200 /tmp/nested.log`
3. Note exact reproduction sequence (keys pressed + order).

Include this in bug reports for fast triage.
