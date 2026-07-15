# Agent Start Here — kalin-wm

kalin-wm is a personal Wayland compositor forked from [dwl](https://codeberg.org/dwl/dwl). It replaces fixed workspaces with an infinite 2D canvas navigated by a viewport camera, adds Niri-style column tiling, and is paired with the Quickshell-based companion shell in `~/environment/quickshell` for the bar, overview, and notifications.

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

## Shell aliases

If you use the home-managed Zsh config, these aliases are defined in `~/home-config/desktop.nix` and are available after the next NixOS rebuild + new terminal:

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
