# kalin-wm

A personal Wayland compositor forked from [dwl](https://codeberg.org/dwl/dwl). kalin-wm keeps the suckless philosophy — compile-time configuration, minimal dependencies, hackable C — and replaces fixed workspaces with an infinite 2D canvas plus Niri-style column tiling. A Quickshell-based companion shell in `~/environment/quickshell` provides the bar, overview, and notifications.

## Quick start

### Build

```bash
cd /home/kalin/environment/kalin-wm
nix develop -c make clean all
```

Run unit tests:

```bash
make test-unit
```

### Run nested (inside an existing graphical session)

```bash
./scripts/run-nested
```

If the script hangs, use the underlying command directly:

```bash
WLR_BACKENDS=wayland ./build/kalin-wm -d
```

To test with the Quickshell bar loaded:

```bash
QS_CONFIG_PATH=/home/kalin/environment/quickshell \
  WLR_BACKENDS=wayland ./build/kalin-wm -d -s 'qs & foot --server'
```

### Run on a TTY

```bash
./scripts/run-tty [secs]
```

Default timeout is 30 s; use `0` to disable it. Logs go to `/tmp/kalin-wm-<timestamp>.log`.

### Automated real-VT test on tty3

```bash
./scripts/test-tty3 [secs]
```

Starts kalin-wm on VT 3 with Quickshell + foot, captures logs, and returns to the original VT. Requires membership in the `tty` group (configured in `~/home-config/users.nix`).

## Shell aliases

If you use the home-managed Zsh config, these aliases are available after `sudo nixos-rebuild switch --flake /home/kalin/home-config#KalinBook` and opening a new terminal:

| Alias | What it does |
|-------|--------------|
| `kalin-code` | `cd ~/environment/kalin-wm` |
| `kalin-shell` | `cd ~/environment/quickshell` |
| `kalin-vm` | `cd ~/environment/test-vm` |
| `kalin-home` | `cd ~/home-config` |
| `kalin-build` | build kalin-wm |
| `kalin-test` | run unit tests |
| `kalin-nested` | run nested compositor |
| `kalin-tty` | run on current TTY |
| `kalin-tty3` | automated VT 3 test |
| `kalin-vm-build` | build the test VM |
| `kalin-vm-run` | run the test VM headless for 60 s |
| `kalin-vm-logs` | tail VM compositor + Quickshell logs |
| `kalin-rebuild` | `sudo nixos-rebuild switch ...` |
| `kalin-rebuild-build` | `nixos-rebuild build ...` |

### Test with the real QEMU/KVM VM

The safest end-to-end test is the VM in `~/environment/test-vm`. It boots a real NixOS system with kalin-wm as the session compositor on a virtual DRM GPU, so it exercises seat/input/GL without touching the host.

```bash
cd ~/environment/test-vm
nix build .#vm
mkdir -p /tmp/kalin-vm/shared
QEMU_OPTS="-display none -serial stdio" ./result/bin/run-kalin-test-vm
```

Logs stream to the host at `/tmp/kalin-vm/kalin-wm.log` and `/tmp/kalin-vm/quickshell.log`. Remove `QEMU_OPTS` to see the graphical window.

### Activate the NixOS login session on the host

Only after VM tests pass:

```bash
sudo nixos-rebuild switch --flake /home/kalin/home-config#KalinBook
```

Then select **kalin-wm** from `ly` at login.

## Default keybindings

`Super` is the Windows/Command key.

| Key | Action |
|-----|--------|
| `Super+T` | Open terminal (`foot`) |
| `Super+P` | Open launcher (`fuzzel`) |
| `Super+O` | Toggle Quickshell overview |
| `Super+Escape` | Quit the compositor |
| `Super+Q` | Close focused window |
| `Super+E` | Toggle fullscreen |
| `Super+Arrows` | Directional focus |
| `Super+Shift+Arrows` / `Super+Shift+HJKL` | Pan camera |
| `Super+equal` / `Super+minus` | Zoom in / out |
| `Super+0` | Fit all windows |
| `Super+BackSpace` | Reset camera |
| `Super+Z` / `Super+Shift+Z` | Toggle follow mode / follow-new-windows |
| `Ctrl+Left` / `Ctrl+Right` | Move focused window between columns |
| `Super+[` / `Super+]` | Narrow / widen focused window |
| `Super+Shift+{` / `Super+Shift+}` | Shorten / lengthen focused window |
| `Super+C` | Enter crop mode (Escape or `Super+Shift+R` to cancel) |

See `code/config/config.h` for the full configuration.

## Project layout

- `code/src/dwl.c` + `code/src/modules/` — compositor source
- `code/include/` — headers (`kalin.h` is the umbrella)
- `code/config/` — compile-time configuration
- `scripts/` — development helpers (`run-nested`, `run-tty`, `build`, `test`)
- `tests/` — unit and integration tests
- `obsidian/` — design vault and project text model
- `docs/` — man page, desktop entry, changelog, manual testing guide

## Status

**Version:** 0.8-dev  
**MVP:** complete (infinite canvas, pan/zoom, column + anchored windows, crop mode, multi-monitor, Quickshell integration)  
**v1.0:** in progress

See `obsidian/roadmap.md` for open work and `obsidian/ledger.md` for recent progress.

---

*License: GNU GPL v3 (same as dwl)*
