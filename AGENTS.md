# Agent Start Here — kalin-wm

kalin-wm is a personal Wayland compositor forked from [dwl](https://codeberg.org/dwl/dwl). It replaces fixed workspaces with an infinite 2D canvas navigated by a viewport camera. Windows are **freely positioned** on that canvas — placed on spawn (to the right of their parent, on the cursor, or centered) and linked into a spawn "connection graph" for directional focus and component dragging. There is **no column/tiling layout**: `arrange()` only manages visibility and z-order, not a tiled layout (the old column-layout/anchored-window split was removed). Dialog/transient/modal/fixed-size windows float as screen-space overlays (see `client_is_float_type()` + `c->isfloating`). It is paired with a companion shell (bar/overview/notifications).

This file is the entry point for coding agents. Deeper design context lives in the `obsidian/` vault; consult it only when you need it.

## Build

```bash
cd /home/kalin/environment/kalin-wm
nix develop -c make clean all
```

Expected outcome: `build/kalin-wm` exists and the build exits 0.

Run unit tests:

```bash
make test-unit
```

Expected outcome: 18 tests pass, 0 failures.

## Run nested (development smoke test)

From inside an existing X11 or Wayland session:

```bash
./scripts/run-nested
```

If the interactive script hangs or you want a direct command:

```bash
WLR_BACKENDS=wayland ./build/kalin-wm -d
```

To also load the Quickshell bar while nested:

```bash
QS_CONFIG_PATH=/home/kalin/environment/quickshell \
  WLR_BACKENDS=wayland ./build/kalin-wm -d -s 'qs & foot --server'
```

Expected outcome: a nested window appears, `Super+T` opens `foot`, `Super+P` opens `fuzzel`, and `Super+Escape` quits.

## Run on a real TTY

```bash
./scripts/run-tty [secs]
```

- Default timeout is 30 s; `0` disables the timeout.
- The script starts `seatd` if it is not running.
- Logs are written to `/tmp/kalin-wm-<timestamp>.log`.

For a bare binary run (not recommended for first test):

```bash
./build/kalin-wm
```

## Automated real-VT test on tty3

`./scripts/test-tty3` starts kalin-wm on VT 3 via `openvt`, with the Quickshell bar and a `foot` server, runs it for a configurable timeout, captures logs, and switches back to the original VT.

```bash
./scripts/test-tty3          # 30 s timeout
./scripts/test-tty3 120      # 120 s timeout
./scripts/test-tty3 0        # no timeout; quit with Super+Escape
```

Requirements:
- You must be in the `tty` group so `openvt` and `chvt` can access VTs.
- The user account is configured in `~/home-config/users.nix`; run `sudo nixos-rebuild switch --flake /home/kalin/home-config#KalinBook` to apply the group change.

Logs: `/tmp/kalin-tty3-test/kalin-wm.log` and `/tmp/kalin-tty3-test/quickshell.log`.

Expected output ends with `PASS: Quickshell configuration loaded` and `PASS: Compositor log shows no crash`.

## Validate with the test VM (preferred)

The safest way to test a real DRM-backed session without touching the host is the QEMU/KVM VM in `~/environment/test-vm`.

### Agentic testing: drive it with `vmctl` (do this, not the manual boot below)

`scripts/vmctl.py` drives the VM entirely from the host — hypervisor-level input (QMP) + VNC framebuffer capture — so an agent can boot, act, and screenshot with no guest cooperation. **This is the standard automated-testing path; prefer it over the manual boot.**

```bash
cd /home/kalin/environment/test-vm
nix flake update kalin-wm && nix build .#vm      # only when compositor code changed
python3 scripts/vmctl.py up                       # boot headless, wait for the session
python3 scripts/vmctl.py shot out.png             # capture a PNG you can Read
python3 scripts/vmctl.py key meta_l t             # a chord, e.g. Super+T (foot)
python3 scripts/vmctl.py click 600 380            # click a window to focus it (coords are 1280x800)
python3 scripts/vmctl.py type "echo hi"           # type into the focused window
python3 scripts/vmctl.py down                      # power off
```

Gotchas an agent WILL hit (all learned the hard way):
- **Leftover VMs hold the qcow2 write lock** → the next `up` fails with `Failed to get "write" lock`, and the stale QEMU's process name is dot-prefixed (`.qemu-system-x8`), so an *exact*-name `pkill`/kill misses it. Match a `*qemu*` substring or kill by PID; `vmctl down` between runs avoids this.
- **`type` drops characters on long strings** — keep typed commands short, or split them.
- **GUI apps for float/dialog testing:** the VM has no Xwayland, so a browser needs `MOZ_ENABLE_WAYLAND=1`; GTK apps need `GDK_BACKEND=wayland` (and the virgl GPU has no Vulkan — GTK4 apps may need `GSK_RENDERER=cairo`, or just use Firefox's own dialogs). Firefox's profile lives on the persistent qcow2 — after unclean kills it corrupts and crashes on start; `rm test-vm/kalin-test.qcow2` for a fresh disk.
- A verified example: launch Firefox, `Ctrl+O` (a transient file-chooser → floats), zoom the canvas out (`Super+Ctrl+minus`) and confirm the dialog stays 1:1 while the canvas shrinks.

### Manual boot (fallback, e.g. for a graphical window)

```bash
cd /home/kalin/environment/test-vm
# If you changed the kalin-wm tree since the last VM build, update the lock entry:
nix flake update kalin-wm
nix build .#vm
mkdir -p /tmp/kalin-vm/shared
# Headless, GL-accelerated smoke run. Remove QEMU_OPTS to open the graphical window.
timeout 60s env QEMU_OPTS="-display egl-headless,gl=on" ./result/bin/run-kalin-test-vm
```

- The VM autologins as `tester` / `test` on tty1 and immediately starts kalin-wm + Quickshell + `foot`.
- Host-readable logs: `/tmp/kalin-vm/kalin-wm.log` and `/tmp/kalin-vm/quickshell.log`.
- To stop the VM, kill the QEMU process or close the window.

Headless health check:

```bash
# after the VM has booted
grep -i "Configuration Loaded" /tmp/kalin-vm/quickshell.log
tail -20 /tmp/kalin-vm/kalin-wm.log
```

Expected: `Configuration Loaded` appears in the Quickshell log and the kalin-wm log shows no segfault.

## Activate the NixOS login session on the host

Only do this after the VM tests pass and the user explicitly asks for it.

The session is defined in `/home/kalin/home-config/display.nix`. It installs a `kalin-wm-session` wrapper that starts `kalin-wm` together with the Quickshell bar (`qs`) and a `foot --server`, and registers `kalin-wm` as a login option in `ly`.

> **Do not run this automatically. Ask the user for explicit approval first.**

```bash
sudo nixos-rebuild switch --flake /home/kalin/home-config#KalinBook
```

After the rebuild, the user can select **kalin-wm** from `ly` at login.

## Verify a kalin-wm login session

After logging in via `ly` with **kalin-wm** selected (or booting the test VM), confirm:

- [ ] The Quickshell bar is visible at the bottom of the screen.
- [ ] `Super+T` opens a terminal (`foot`).
- [ ] `Super+P` opens the launcher (`fuzzel`).
- [ ] `Super+O` toggles the Quickshell overview.
- [ ] The taskbar lists running applications.
- [ ] `Super+Escape` quits the session.

If any item fails, check the VM logs (`/tmp/kalin-vm/*.log`) or, on the host, `journalctl --user -u kalin-wm` and Quickshell logs.

## Getting a new build actually running (ly caches sessions)

`nixos-rebuild switch` does **not** change what the login screen launches.
`ly` enumerates `/etc/wayland-sessions/` once, when the display-manager service
starts (boot), and keeps launching that generation's `kalin-wm-session` wrapper
— which hard-codes a store path. Logging out and back in therefore still starts
the **old** compositor binary; this hid an 8-day-old build (pre-amber-rice,
pre-paper-knob) behind an up-to-date system for a week (found 2026-07-25).

To actually run a new build:

- `sudo systemctl restart display-manager` (ends the session → greeter → log in), or
- reboot, or
- for dev iteration, don't involve ly at all: `kalinwm` from a VT runs
  `build/kalin-wm` directly (see obsidian/implementation/dev-restart.md).

Check which one is live: compare
`pgrep -af bin/kalin-wm` against `grep Exec /etc/wayland-sessions/kalin-wm.desktop`.

## Shell aliases

If you use the home-managed Zsh config, these aliases are defined in `~/.zshrc` and are available in any new terminal:

- `kalin-code`, `kalin-shell`, `kalin-vm`, `kalin-home` — cd into the main repos.
- `kalin-build`, `kalin-test` — build and unit-test kalin-wm.
- `kalin-nested`, `kalin-tty`, `kalin-tty3` — run the compositor nested, on the current TTY, or on VT 3.
- `kalin-vm-build`, `kalin-vm-run`, `kalin-vm-logs` — build/run/check the QEMU VM.
- `kalin-rebuild`, `kalin-rebuild-build` — host NixOS rebuild helpers.

## Key files and pointers

- `code/config/config.h` — compile-time keybindings and constants.
- `code/config/config.def.h` — upstream defaults; copy to `config.h` to customize.
- `code/src/dwl.c` + `code/src/modules/` — compositor source.
- `obsidian/plan/kalin-wm.md` — project goal note.
- `obsidian/plan/agent-workflow.md` — coding rules and workflow.
- `obsidian/implementation/keybindings.md` — keybinding reference.
- `obsidian/implementation/nixos-session.md` — how the login session is wired.
- `obsidian/implementation/quickshell-shell.md` — shell integration details.
- `obsidian/implementation/build-system.md` — build and flake details.
- `obsidian/plan/roadmap.md` — open work.
- `obsidian/implementation/dev-restart.md` — live dev-session restart procedure.
- `obsidian/implementation/ledger.md` — frozen archive of past decisions (the vault graph is the running record now).
