# headless-mc — headless multi-output test harness

Runs kalin-wm on wlroots' **headless** backend with N virtual outputs and the
pixman software renderer. No GPU, no parent Wayland/X session, nothing drawn on
the real desktop — so an agent (or CI) can test anything needing multiple
monitors without a login session and without a second physical display. This
fills the gap the QEMU [test VM](../../../test-vm) can't: its single virtio GPU
exposes only one output.

Everything is driven over kalin-wm's IPC socket, including a deterministic
pointer `warp` (added for this — the headless backend has no real pointer, and
`selmon`/camera-input follows the cursor). Screenshots are per-output via `grim`
as an ordinary client of the nested compositor.

## Use

```bash
export XDG_RUNTIME_DIR=/run/user/$(id -u)
H=~/environment/kalin-wm/tools/headless-mc/hmc.py

python3 $H up --outputs 2 --spawn foot     # boot; 2 outputs, a foot on each-ish
python3 $H outputs                          # HEADLESS-1 1280,0 …  HEADLESS-2 0,0 …
python3 $H warp 1920 360                     # move pointer onto HEADLESS-1 (sets selmon)
python3 $H ipc 'pan 300 0'                   # pan the monitor under the cursor
python3 $H state viewports                    # per-output camera state (x/y/zoom)
python3 $H shot HEADLESS-1 /tmp/h1.png        # grim one output
python3 $H down
```

Requires `grim` on PATH and a writable `$XDG_RUNTIME_DIR`. Override the binary
with `HMC_BIN=` and the state dir with `HMC_DIR=` (default `/tmp/kalin-hmc`).

## What it proves (and can't)

- **Can:** output layout/geometry, per-monitor camera state, camera isolation
  (pan/zoom one output, assert the other is unchanged), per-output rendering,
  anything drivable via IPC + observable via state/screenshot.
- **Can't:** GPU/DRM-specific paths (software render only), real input-device
  event plumbing (uses the `warp` IPC hook, not libinput), and anything needing
  a real seat/logind session (brightness, etc. — use the VM or the real host).

## Relationship to the other test paths

- Unit tests (`make test-unit`) — pure logic, no compositor.
- **headless-mc (this)** — multi-output compositor behavior, host-automated,
  no session. First choice for multi-monitor / camera work.
- [test VM](../../../test-vm) `vmctl` — real DRM/GL, single output, the
  trustworthy end-to-end check for GPU/session-dependent behavior.
- Nested (`WLR_BACKENDS=wayland`) — quick, but renders *onto* the real session
  and can't take exclusive input; prefer headless-mc for anything scripted.
