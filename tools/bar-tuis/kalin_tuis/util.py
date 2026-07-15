"""Backend plumbing shared by the panel TUIs."""
from __future__ import annotations

import asyncio
import json
import os
import socket
import subprocess

KALIN_IPC_SOCKET = os.environ.get("KALIN_IPC_SOCKET", "")


def run(argv: list[str], timeout: float = 5.0, check: bool = True,
        input_text: str | None = None) -> str:
    """Capture stdout; surface stderr in the raised error so panels can put
    the real failure in their status line instead of a bare exit code."""
    proc = subprocess.run(argv, capture_output=True, text=True, timeout=timeout,
                          input=input_text)
    if check and proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or f"{argv[0]} exited {proc.returncode}")
    return proc.stdout


async def run_async(argv: list[str], timeout: float = 45.0) -> str:
    """run(), but awaitable — for slow operations (nmcli connect can block
    for tens of seconds) that must not freeze the UI's event loop."""
    proc = await asyncio.create_subprocess_exec(
        *argv, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
    try:
        stdout, stderr = await asyncio.wait_for(proc.communicate(), timeout)
    except asyncio.TimeoutError:
        proc.kill()
        raise RuntimeError(f"{argv[0]} timed out after {timeout:.0f}s") from None
    if proc.returncode != 0:
        raise RuntimeError(stderr.decode().strip() or f"{argv[0]} exited {proc.returncode}")
    return stdout.decode()


def kalin_ipc_state() -> dict:
    """One request/response round trip: connect, read the next broadcast
    line (kalin-wm sends one on every state change, so this is effectively
    "read the current state"), disconnect — spawn/query, one snapshot, done,
    rather than holding a long-lived connection across the poll interval."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.settimeout(5)
        s.connect(KALIN_IPC_SOCKET)
        f = s.makefile("r")
        line = f.readline()
    return json.loads(line)


def kalin_ipc_send(cmd: str) -> None:
    """Fire-and-forget: the command handlers don't ack per-command, only via
    the continuous state stream (which the next poll picks up naturally)."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.settimeout(5)
        s.connect(KALIN_IPC_SOCKET)
        s.sendall((cmd + "\n").encode())
