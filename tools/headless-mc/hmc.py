#!/usr/bin/env python3
"""Headless multi-output test harness for kalin-wm.

Runs kalin-wm on wlroots' *headless* backend with N virtual outputs and the
pixman software renderer — no GPU, no parent Wayland/X session, nothing on
the real desktop. An agent can boot it, drive the compositor entirely over
its IPC socket (including a deterministic pointer `warp` so selmon is
controllable without synthetic input), spawn clients, and screenshot each
output independently with grim. This is the environment for testing anything
that needs multiple monitors (e.g. multi-camera) where the QEMU test VM's
single virtio GPU can't help.

Usage:
    hmc.py up [--outputs N] [--spawn CMD]   boot; prints WAYLAND_DISPLAY + socket
    hmc.py ipc  '<line>'                     send one IPC command line
    hmc.py state [key]                        print the latest state (or one key)
    hmc.py warp X Y                           move the pointer (sets selmon)
    hmc.py shot OUTPUT OUT.png                grim one output to a PNG
    hmc.py outputs                            list output names + geometry
    hmc.py down                               kill the compositor

State is kept in $HMC_DIR (default /tmp/kalin-hmc): the compositor pid and its
WAYLAND_DISPLAY, so subcommands find the running instance.
"""
import json
import os
import socket
import subprocess
import sys
import time

DIR = os.environ.get("HMC_DIR", "/tmp/kalin-hmc")
BIN = os.environ.get("HMC_BIN",
                     os.path.expanduser("~/environment/kalin-wm/build/kalin-wm"))
PIDFILE = os.path.join(DIR, "pid")
WDFILE = os.path.join(DIR, "wayland_display")
LOG = os.path.join(DIR, "kalin-wm.log")
RUNTIME = os.environ.get("XDG_RUNTIME_DIR", "/run/user/%d" % os.getuid())


def _read(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except FileNotFoundError:
        return None


def _ipc_socket():
    wd = _read(WDFILE)
    if not wd:
        raise SystemExit("no running instance (run `hmc.py up` first)")
    return os.path.join(RUNTIME, f"kalin-ipc-{wd}.sock")


def ipc_send(line):
    with socket.socket(socket.AF_UNIX) as s:
        s.settimeout(5)
        s.connect(_ipc_socket())
        s.sendall((line + "\n").encode())
        time.sleep(0.2)  # let the compositor drain before we close


def ipc_state():
    with socket.socket(socket.AF_UNIX) as s:
        s.settimeout(5)
        s.connect(_ipc_socket())
        return json.loads(s.makefile("r").readline())


def up(args):
    outputs = 1
    spawn = "true"
    i = 0
    while i < len(args):
        if args[i] == "--outputs":
            outputs = int(args[i + 1]); i += 2
        elif args[i] == "--spawn":
            spawn = args[i + 1]; i += 2
        else:
            i += 1
    os.makedirs(DIR, exist_ok=True)
    # Snapshot existing ipc sockets so we can identify the new one.
    before = set(f for f in os.listdir(RUNTIME) if f.startswith("kalin-ipc-"))
    env = dict(os.environ)
    env.pop("WAYLAND_DISPLAY", None)
    env.pop("DISPLAY", None)
    env["WLR_BACKENDS"] = "headless"
    env["WLR_RENDERER"] = "pixman"
    env["WLR_HEADLESS_OUTPUTS"] = str(outputs)
    log = open(LOG, "wb")
    proc = subprocess.Popen([BIN, "-d", "-s", spawn], env=env,
                            stdout=log, stderr=log, stdin=subprocess.DEVNULL,
                            start_new_session=True)
    with open(PIDFILE, "w") as f:
        f.write(str(proc.pid))
    # Wait for the new ipc socket to appear.
    deadline = time.time() + 15
    wd = None
    while time.time() < deadline:
        now = set(f for f in os.listdir(RUNTIME) if f.startswith("kalin-ipc-"))
        new = now - before
        if new:
            sock = sorted(new)[-1]
            wd = sock[len("kalin-ipc-"):-len(".sock")]
            break
        time.sleep(0.3)
    if not wd:
        raise SystemExit("compositor did not create an IPC socket; see " + LOG)
    with open(WDFILE, "w") as f:
        f.write(wd)
    time.sleep(1.0)  # let outputs finish modesetting
    print(f"up: pid={proc.pid} WAYLAND_DISPLAY={wd} outputs={outputs}")
    print(f"  IPC:  {os.path.join(RUNTIME, f'kalin-ipc-{wd}.sock')}")


def shot(args):
    output, out_png = args[0], args[1]
    wd = _read(WDFILE)
    if not wd:
        raise SystemExit("no running instance")
    env = dict(os.environ, WAYLAND_DISPLAY=wd, XDG_RUNTIME_DIR=RUNTIME)
    subprocess.run(["grim", "-o", output, out_png], env=env, check=True)
    print(f"wrote {out_png}")


def down(args):
    pid = _read(PIDFILE)
    if pid:
        try:
            os.kill(int(pid), 15)
            time.sleep(0.3)
            os.kill(int(pid), 15)
        except ProcessLookupError:
            pass
    for f in (PIDFILE, WDFILE):
        try:
            os.unlink(f)
        except FileNotFoundError:
            pass
    print("down")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    action, rest = sys.argv[1], sys.argv[2:]
    if action == "up":
        up(rest)
    elif action == "ipc":
        ipc_send(rest[0])
    elif action == "warp":
        ipc_send(f"warp {rest[0]} {rest[1]}")
    elif action == "state":
        st = ipc_state()
        print(json.dumps(st[rest[0]] if rest else st, indent=2))
    elif action == "outputs":
        for o in ipc_state()["outputs"]:
            print(f"{o['name']:14} {o['x']},{o['y']}  {o['width']}x{o['height']}")
    elif action == "shot":
        shot(rest)
    elif action == "down":
        down(rest)
    else:
        raise SystemExit(f"unknown action {action!r}")


if __name__ == "__main__":
    main()
