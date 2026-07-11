---
name: kalin-wm
description: Work on kalin-wm, Kalin's personal Wayland compositor (a dwl fork with an infinite 2D canvas) and its Quickshell companion shell. Use when building, testing, running, screenshotting, or hacking on the compositor in ~/environment/kalin-wm, or driving its QEMU test VM.
---

# Working on kalin-wm

kalin-wm is a personal Wayland compositor forked from [dwl](https://codeberg.org/dwl/dwl):
it replaces fixed workspaces with an **infinite 2D canvas** navigated by a viewport
camera, adds a connection-graph-based window layout, and pairs with a **Quickshell** shell
(`~/environment/quickshell`) for the bar, overview, and notifications. It is
**Wayland-only** (the XWayland/X11 client paths from dwl have been removed).

Repo: `~/environment/kalin-wm`. The single source of truth for workflow is
`~/environment/kalin-wm/AGENTS.md` — **read it first**; deeper design context is
in `~/environment/kalin-wm/obsidian/`. This skill is the fast path; AGENTS.md wins
on any conflict.

## Orient before editing

- `code/src/dwl.c` — the compositor core (still the bulk of the dwl heritage,
  though shrinking as features get extracted). `#include`s some modules
  mid-file (`client_inline.h`).
- `code/src/modules/` — extracted features: `viewport/`, `crop/`, `input/`,
  `ui/`, `layout/`, `screenshot/`, `protocols/`, `binds/`, `ipc.c`,
  `foreign_toplevel.c`, `backlight.c`, `capture.c`, `session_lock.c`.
- `code/include/` — headers. `kalin.h` is a parallel umbrella used by the
  modules (dwl.c does NOT include it the same way — it defines `DWL_INTERNAL`
  before including it, which skips the "public extern API" section since dwl.c
  owns those symbols itself); watch for that split when changing shared structs.
- `code/config/config.h` — compile-time keybindings/constants (`config.def.h` is
  the template). Runtime keybindings live in `~/.config/kalin-wm/binds.conf`
  (DSL parsed by `code/src/modules/binds/`), seeded from
  `code/config/default_binds.h` on first run only.

Match the surrounding suckless C style: tabs, declarations-before-code (the build
uses `-Wdeclaration-after-statement`), minimal abstraction.

## Build & unit test

```bash
cd ~/environment/kalin-wm
nix develop -c make clean all      # -> build/kalin-wm, exits 0
nix develop -c make test-unit      # -> all tests pass, 0 failures
```

Always get a green build + unit tests before and after a change. The build is
strict (`-Werror=` on several classes); fix warnings you introduce.

## Run it

Fastest inner loop — nested inside the current session:

```bash
QS_CONFIG_PATH=~/environment/quickshell \
  WLR_BACKENDS=wayland ./build/kalin-wm -d -s 'qs & foot --server'
```

`Super+T` = foot, `Super+P` = fuzzel, `Super+O` = overview, `Super+Escape` = quit.

## Test in the VM (real DRM/GL, host-automated)

The trustworthy end-to-end test is the QEMU/KVM NixOS VM in `~/environment/test-vm`
— it boots kalin-wm as the real session compositor on a virtual GPU. It consumes
kalin-wm via a flake input, so **refresh the lock after changing the tree**:

```bash
cd ~/environment/test-vm
nix flake update kalin-wm          # picks up the kalin-wm working tree
nix build .#vm
```

Drive it entirely from the host with `scripts/vmctl.py` (no guest tooling):

```bash
python3 scripts/vmctl.py up                     # boot headless, wait for compositor
python3 scripts/vmctl.py shot /tmp/kalin-vm/a.png
python3 scripts/vmctl.py key meta_l t           # inject a chord (Super+T)
python3 scripts/vmctl.py type "echo hi"        # type into the focused window
python3 scripts/vmctl.py down                    # power off
```

`vmctl` reads a PNG straight from QEMU's framebuffer, so you can `Read` the
screenshot to see the actual desktop. Then verify health:

- `grep -i "Configuration Loaded" /tmp/kalin-vm/quickshell.log` (shell up)
- `tail /tmp/kalin-vm/kalin-wm.log` — should show `viewport …` and no segfault.
- Expected-harmless warnings: no niri/nmcli/UPower/bluez in the VM;
  `xdg-toplevel-icon`/`text-input` protocols not implemented.

### How the automation works (why this combo)

Launch QEMU with `-display egl-headless,gl=on -vnc unix:…/vnc.sock -qmp
unix:…/qmp.sock,server=on,wait=off` (`vmctl up` does this). Then:

- **Input** = QMP `send-key` / `input-send-event` — hypervisor-level, deterministic.
- **Screenshot** = read the **VNC** framebuffer. QMP `screendump` fails with
  *"no surface"* on the GL scanout (it's a dmabuf); `egl-headless` blits the GL
  frame into the VNC surface, which is readable.

Keystrokes hit whatever holds keyboard focus — focus the target window first if
`type` needs to land somewhere specific.

## Testing on the real host (live dev-session restart)

While the compositor is still under active development, Kalin runs it directly
(not via the packaged NixOS session) with the `kalinwm` dev-launcher script —
`kalinwm` rebuilds `build/kalin-wm` if missing and runs
`kalin-wm -s 'qs & foot --server'`. To test a fresh build against the *live*
session:

```bash
# 1. Find and kill the running instance. It has a 2-second exit-confirmation
#    window (same as the real Super+Escape keybind) — a single SIGTERM only
#    arms it, a second one within ~2s actually quits:
ps -ef | grep "kalin-wm/build/kalin-wm -s" | grep -v grep   # find the PID
kill -TERM <pid>; sleep 0.5; kill -TERM <pid>
sleep 2; kill -0 <pid>   # should fail ("no such process") once it's really down

# 2. Relaunch:
export XDG_RUNTIME_DIR=/run/user/1000
nohup /run/current-system/sw/bin/kalinwm > /tmp/kalinwm-restart.log 2>&1 &
disown

# 3. Always open a terminal Kalin can instantly attach to — he keeps a
#    persistent tmux session ("claude") his own Claude Code session runs
#    inside, and expects a foot window pre-attached to it after every
#    restart, not a bare shell:
export WAYLAND_DISPLAY=wayland-0
foot -e tmux a &
disown
```

QML-only changes (Quickshell) don't need any of this — `qs`/`quickshell`
reads `~/environment/quickshell` live; just `pkill -f quickshell-wrapped` and
relaunch `qs` (with `KALIN_IPC_SOCKET`/`QS_CONFIG_PATH`/`WAYLAND_DISPLAY` set)
to pick up an edit, no compositor restart needed.

**Caveat**: a compositor instance launched this way (from a detached
`nohup`/background shell, not Kalin's own login-session terminal) is *not*
part of any systemd-logind session — `GetSessionByPID` fails for it. This is
harmless for most things, but session-authenticated actions (e.g.
`backlight_set()`'s `SetBrightness` D-Bus call) will fail with "Session is
not in foreground, refusing" no matter which session_path is targeted,
because logind checks the *calling process's* own session credentials, not
just an argument. If testing something session-dependent, ask Kalin to
relaunch `kalinwm` from his own real terminal instead of doing it yourself.

**Session-lock testing**: never trigger `loginctl lock-session` or a lock
client against Kalin's live host session — you don't have his unlock
password, so a bug there could lock him out. Test session-lock changes in
the VM instead (`swaylock` + `security.pam.services.swaylock` are set up
there for exactly this).

## Guardrails

- **Never** activate the real host login session (`sudo nixos-rebuild switch …
  home-config#KalinBook`) automatically — only when Kalin explicitly asks, and
  only after VM tests pass.
- **Avoid `nix build`/`nixos-rebuild` churn while the compositor is still
  under active development** — each one leaves a new store path behind and
  Kalin is disk-space-conscious about this. `nix develop -c make` builds
  locally into `build/`, not the Nix store, and is the right tool for the
  iterate-and-test loop above. Save `nix build`/`nixos-rebuild switch` for
  when Kalin explicitly says it's time to land something in `home-config`
  (or ask him if unsure) — don't run them just to "verify the Nix package
  still builds" as a matter of routine.
- Commit only when asked. When removing dwl heritage, prefer focused, buildable
  steps (build + unit tests green at each step) over sweeping deletions.
- Keep the `obsidian/` vault current when work changes the project (ledger +
  object notes), per `~/environment/kalin-wm/AGENTS.md`.
