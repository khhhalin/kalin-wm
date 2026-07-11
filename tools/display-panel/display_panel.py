#!/usr/bin/env python3
"""Display settings panel — Textual TUI replacement for the QML DisplayService
/ DisplayWidget pair (see ~/environment/quickshell/modules/services/DisplayService.qml).

Meant to be docked as a real, borderless Wayland window into the bar's display
panel region by the compositor (see DockedPanel.qml in ~/environment/quickshell
— this script itself has no docking logic, it's just what gets docked).

Backend selection, mirroring KalinViewport.enabled's own check:
  - If $KALIN_IPC_SOCKET is set, we're under kalin-wm — talk to it directly
    over that socket (see ipc.c's "outputs"/"brightness" state fields and the
    "set-output"/"set-brightness" commands) instead of shelling out to
    wlr-randr/brightnessctl. Both read *and write* (resolution/scale/position/
    enabled, brightness) go through the compositor this way; wlr-randr can
    still read kalin-wm's outputs (confirmed working), but writing brightness
    via brightnessctl fails there — the sysfs backlight file is root-owned
    with no group-write udev rule on this host, so the compositor goes
    through systemd-logind's SetBrightness instead (see backlight.c) — the
    IPC command is the only way this TUI can reach that path.
  - Otherwise assume niri and use `niri msg --json outputs` + `brightnessctl`,
    same as before. (niri does NOT implement wlr-output-management, per
    ~/home-config/display.nix's portal comment, so wlr-randr would not work
    there even if present — the inverse of what might be assumed.)
  - If neither backend is usable, show a clear "not supported" state instead
    of crashing.

Run directly: `python3 display_panel.py` (no args). Quit with `q` or Ctrl+C.
"""
from __future__ import annotations

import json
import os
import re
import shutil
import socket
import subprocess
from dataclasses import dataclass, field

from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical, VerticalScroll
from textual.timer import Timer
from textual.widgets import Footer, Header, Label, ProgressBar, Static

POLL_INTERVAL = 2.0
INTERNAL_PANEL_RE = re.compile(r"^(eDP|LVDS|DSI)", re.IGNORECASE)
KALIN_IPC_SOCKET = os.environ.get("KALIN_IPC_SOCKET", "")


@dataclass
class OutputInfo:
    name: str
    description: str = ""
    width: int = 0
    height: int = 0
    refresh: float = 0.0
    scale: float = 1.0
    x: int = 0
    y: int = 0
    enabled: bool = True
    modes: list[tuple[int, int, float]] = field(default_factory=list)  # (width, height, refresh), kalin-wm only
    brightness_device: str = ""
    brightness_percent: int | None = None  # None = not backlight-controllable


@dataclass
class Backend:
    """Which output-listing backend is in play, and why."""

    kind: str  # "kalin-ipc" | "niri" | "unavailable"
    reason: str = ""


def detect_backend() -> Backend:
    """Mirror KalinViewport.enabled: $KALIN_IPC_SOCKET set => kalin-wm."""
    if KALIN_IPC_SOCKET:
        if os.path.exists(KALIN_IPC_SOCKET):
            return Backend("kalin-ipc")
        return Backend("unavailable", f"$KALIN_IPC_SOCKET set but {KALIN_IPC_SOCKET} doesn't exist.")
    if shutil.which("niri"):
        return Backend("niri")
    return Backend("unavailable", "Neither kalin-wm ($KALIN_IPC_SOCKET) nor niri detected.")


def _read_kalin_state() -> dict:
    """One request/response round trip: connect, read the next broadcast
    line (kalin-wm sends one on every state change, so this is effectively
    "read the current state"), disconnect. Mirrors how the other two
    backends work — spawn/query, get one snapshot, done — rather than
    holding a long-lived connection open across Textual's poll interval."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.settimeout(5)
        s.connect(KALIN_IPC_SOCKET)
        f = s.makefile("r")
        line = f.readline()
    return json.loads(line)


def _send_kalin_command(cmd: str) -> None:
    """Fire-and-forget: the command handlers don't ack per-command, only via
    the continuous state stream (which the next poll picks up naturally)."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.settimeout(5)
        s.connect(KALIN_IPC_SOCKET)
        s.sendall((cmd + "\n").encode())


def query_outputs_kalin_ipc() -> tuple[list[OutputInfo], dict | None]:
    """Returns (outputs, brightness) — brightness is {"value","max"} (raw,
    not percent — see backlight.c) or None if no backlight device exists,
    straight from the same state line so they're always consistent with
    each other (no separate round trip the way brightnessctl needed)."""
    state = _read_kalin_state()
    outputs = []
    for o in state.get("outputs", []):
        outputs.append(
            OutputInfo(
                name=o.get("name", "?"),
                description="",  # kalin-wm's IPC doesn't carry make/model
                width=o.get("width", 0),
                height=o.get("height", 0),
                refresh=o.get("refresh", 0.0),
                scale=o.get("scale", 1.0),
                x=o.get("x", 0),
                y=o.get("y", 0),
                enabled=o.get("enabled", True),
                modes=[(m["width"], m["height"], m["refresh"]) for m in o.get("modes", [])],
            )
        )
    outputs.sort(key=lambda o: o.x)
    return outputs, state.get("brightness")


def set_output_mode_kalin(o: OutputInfo, width: int, height: int, refresh: float) -> None:
    """Cycle/apply a resolution — used by the 'm' keybind. Scale/position/
    enabled pass through unchanged (see ipc.c's set-output doc: <=0 means
    "leave as-is" for scale, but x/y/enabled are always applied, so pass the
    output's own current values back for those)."""
    _send_kalin_command(
        f"set-output {o.name} {width} {height} {refresh} 0 {o.x} {o.y} {1 if o.enabled else 0}"
    )


def set_brightness_kalin(raw_value: int) -> None:
    _send_kalin_command(f"set-brightness {raw_value}")


def query_outputs_niri() -> list[OutputInfo]:
    proc = subprocess.run(
        ["niri", "msg", "--json", "outputs"], capture_output=True, text=True, timeout=5
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "niri msg exited non-zero")
    raw = json.loads(proc.stdout)
    outputs = []
    for name, o in raw.items():
        modes = o.get("modes", [])
        idx = o.get("current_mode")
        mode = modes[idx] if idx is not None and 0 <= idx < len(modes) else None
        logical = o.get("logical") or {}
        outputs.append(
            OutputInfo(
                name=o.get("name", name),
                description=f"{o.get('make', '')} {o.get('model', '')}".strip(),
                width=mode["width"] if mode else 0,
                height=mode["height"] if mode else 0,
                refresh=(mode["refresh_rate"] / 1000.0) if mode else 0.0,
                scale=logical.get("scale", 1.0),
                x=logical.get("x", 0),
                y=logical.get("y", 0),
            )
        )
    outputs.sort(key=lambda o: o.x)
    return outputs


def query_backlights() -> dict[str, dict]:
    """device -> {current, percent, max}, same parsing as DisplayService.qml."""
    if not shutil.which("brightnessctl"):
        return {}
    proc = subprocess.run(
        ["brightnessctl", "-m", "-l", "-c", "backlight"],
        capture_output=True,
        text=True,
        timeout=5,
    )
    if proc.returncode != 0:
        return {}
    devices: dict[str, dict] = {}
    for line in proc.stdout.strip().splitlines():
        parts = line.strip().split(",")
        if len(parts) < 5:
            continue
        name, _cls, current, percent_str, maximum = parts[:5]
        try:
            devices[name] = {
                "current": int(current),
                "percent": int(percent_str.rstrip("%")),
                "max": int(maximum),
            }
        except ValueError:
            continue
    return devices


def merge_backlights(outputs: list[OutputInfo], backlights: dict[str, dict]) -> None:
    """Heuristic pairing: internal panels (eDP/LVDS/DSI) get backlight devices
    in order, matching DisplayService.qml's _merge()."""
    device_names = list(backlights.keys())
    idx = 0
    for output in outputs:
        if INTERNAL_PANEL_RE.match(output.name) and idx < len(device_names):
            dev = device_names[idx]
            idx += 1
            output.brightness_device = dev
            output.brightness_percent = backlights[dev]["percent"]


def merge_kalin_brightness(outputs: list[OutputInfo], brightness: dict | None) -> None:
    """kalin-wm's IPC exposes a single backlight device (see backlight.c —
    it just picks the first /sys/class/backlight/* entry), not a per-device
    map like brightnessctl -m, so there's nothing to heuristically pair by
    name: it's simply the first internal panel, same convention as
    merge_backlights() above uses once idx==0."""
    if not brightness or brightness.get("max", 0) <= 0:
        return
    for output in outputs:
        if INTERNAL_PANEL_RE.match(output.name):
            output.brightness_device = "kalin-ipc"  # sentinel: routes writes through set_brightness_kalin
            output.brightness_percent = round(brightness["value"] / brightness["max"] * 100)
            break


def set_brightness_niri(device: str, percent: int) -> None:
    percent = max(0, min(100, percent))
    subprocess.run(
        ["brightnessctl", "-d", device, "s", f"{percent}%"],
        capture_output=True,
        timeout=5,
    )


class OutputCard(Vertical):
    """One panel's info + brightness control."""

    def __init__(self, output: OutputInfo) -> None:
        super().__init__(classes="output-card")
        self.output = output

    def compose(self) -> ComposeResult:
        o = self.output
        mode_str = f"{o.width}x{o.height} @ {o.refresh:.2f}Hz" if o.width else "unknown mode"
        yield Label(f"[b]{o.name}[/b]  {o.description}", classes="output-title")
        yield Label(f"{mode_str}   scale {o.scale:g}x   pos ({o.x}, {o.y})", classes="output-meta")
        if o.brightness_percent is not None:
            with Horizontal(classes="brightness-row"):
                yield Label("Brightness", classes="brightness-label")
                yield ProgressBar(
                    total=100,
                    show_eta=False,
                    id=f"bar-{o.name}",
                )
                yield Label(f"{o.brightness_percent}%", id=f"pct-{o.name}", classes="brightness-pct")
        else:
            yield Label("No backlight control on this output.", classes="output-meta dim")

    def on_mount(self) -> None:
        if self.output.brightness_percent is not None:
            bar = self.query_one(f"#bar-{self.output.name}", ProgressBar)
            bar.update(progress=self.output.brightness_percent)

    def update_brightness(self, percent: int) -> None:
        self.output.brightness_percent = percent
        bar = self.query_one(f"#bar-{self.output.name}", ProgressBar)
        bar.update(progress=percent)
        self.query_one(f"#pct-{self.output.name}", Label).update(f"{percent}%")


class DisplayPanelApp(App):
    """Textual TUI: connected outputs + per-panel brightness."""

    CSS = """
    Screen {
        background: $surface;
    }

    #status-line {
        padding: 0 1;
        color: $text-muted;
        height: 1;
    }

    #outputs {
        padding: 1;
    }

    .output-card {
        border: round $primary;
        padding: 1 2;
        margin-bottom: 1;
        height: auto;
    }

    .output-title {
        text-style: bold;
    }

    .output-meta {
        color: $text-muted;
    }

    .output-meta.dim {
        color: $text-disabled;
    }

    .brightness-row {
        height: 1;
        margin-top: 1;
        align-vertical: middle;
    }

    .brightness-label {
        width: 12;
    }

    .brightness-pct {
        width: 5;
        text-align: right;
    }

    ProgressBar {
        width: 1fr;
        margin: 0 1;
    }

    #unavailable {
        padding: 2;
        color: $text-muted;
        border: round $error;
        margin: 1;
    }
    """

    BINDINGS = [
        ("q", "quit", "Quit"),
        ("ctrl+c", "quit", "Quit"),
        ("up", "adjust_brightness_up", "Brightness +5"),
        ("down", "adjust_brightness_down", "Brightness -5"),
        ("m", "cycle_mode", "Cycle resolution"),
        ("r", "refresh_now", "Refresh"),
    ]

    def __init__(self) -> None:
        super().__init__()
        self.backend = detect_backend()
        self._poll_timer: Timer | None = None
        self._outputs: list[OutputInfo] = []
        self._kalin_brightness_max = 0  # raw max, for percent<->raw conversion on write

    def compose(self) -> ComposeResult:
        yield Header(show_clock=False)
        yield Label("", id="status-line")
        yield VerticalScroll(id="outputs")
        yield Footer()

    def on_mount(self) -> None:
        self.title = "Display Settings"
        self._refresh()
        self._poll_timer = self.set_interval(POLL_INTERVAL, self._refresh)

    def action_refresh_now(self) -> None:
        self._refresh()

    def action_adjust_brightness_up(self) -> None:
        self._adjust_selected_brightness(+5)

    def action_adjust_brightness_down(self) -> None:
        self._adjust_selected_brightness(-5)

    def action_cycle_mode(self) -> None:
        """kalin-wm only — cycles the first output's resolution through its
        advertised modes (see set-output's doc comment in ipc.c). No niri
        equivalent: this app never had output *writing* for niri, only
        reading, and niri's own IPC has a completely different reconfigure
        shape that's out of scope here."""
        if self.backend.kind != "kalin-ipc" or not self._outputs:
            return
        output = self._outputs[0]
        if len(output.modes) < 2:
            return
        current = (output.width, output.height, output.refresh)
        try:
            idx = output.modes.index(current)
        except ValueError:
            idx = -1
        width, height, refresh = output.modes[(idx + 1) % len(output.modes)]
        set_output_mode_kalin(output, width, height, refresh)
        self._refresh()

    def _adjust_selected_brightness(self, delta: int) -> None:
        controllable = [o for o in self._outputs if o.brightness_device]
        if not controllable:
            return
        output = controllable[0]  # single-monitor laptop case; first controllable panel
        new_percent = max(0, min(100, (output.brightness_percent or 0) + delta))
        if self.backend.kind == "kalin-ipc":
            raw = round(new_percent / 100 * self._kalin_brightness_max)
            set_brightness_kalin(raw)
        else:
            set_brightness_niri(output.brightness_device, new_percent)
        output.brightness_percent = new_percent
        for card in self.query(OutputCard):
            if card.output.name == output.name:
                card.update_brightness(new_percent)
                break

    def _refresh(self) -> None:
        status = self.query_one("#status-line", Label)

        if self.backend.kind == "unavailable":
            status.update(f"[red]Unavailable:[/red] {self.backend.reason}")
            self._show_unavailable(self.backend.reason)
            return

        try:
            if self.backend.kind == "kalin-ipc":
                outputs, brightness = query_outputs_kalin_ipc()
                self._kalin_brightness_max = (brightness or {}).get("max", 0)
                merge_kalin_brightness(outputs, brightness)
            else:
                outputs = query_outputs_niri()
                backlights = query_backlights()
                merge_backlights(outputs, backlights)
        except Exception as exc:  # noqa: BLE001 - surface any backend failure in the UI
            status.update(f"[red]Failed to query outputs ({self.backend.kind}):[/red] {exc}")
            self._show_unavailable(f"Failed to query outputs via {self.backend.kind}: {exc}")
            return

        status.update(f"[green]{self.backend.kind}[/green] · {len(outputs)} output(s)")
        self._outputs = outputs
        self._render_outputs(outputs)

    def _show_unavailable(self, reason: str) -> None:
        container = self.query_one("#outputs", VerticalScroll)
        container.remove_children()
        container.mount(Static(reason, id="unavailable"))

    def _render_outputs(self, outputs: list[OutputInfo]) -> None:
        container = self.query_one("#outputs", VerticalScroll)

        existing_names = {
            card.output.name: card for card in container.query(OutputCard)
        }
        new_names = {o.name for o in outputs}

        # Drop cards for outputs that disappeared.
        for name, card in existing_names.items():
            if name not in new_names:
                card.remove()

        for output in outputs:
            card = existing_names.get(output.name)
            if card is None:
                container.mount(OutputCard(output))
            else:
                card.output = output
                if output.brightness_percent is not None:
                    card.update_brightness(output.brightness_percent)


def main() -> None:
    DisplayPanelApp().run()


if __name__ == "__main__":
    main()
