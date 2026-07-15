"""Battery / power panel — replaces the QML SidePanel battery pane
(SystemPanel.qml's batPane, deleted when this landed) with a docked TUI.

Event-driven: subscribes to UPower's DisplayDevice PropertiesChanged over
the system bus via dbus-fast (async, so it drops straight into Textual's
own asyncio loop) with a 30s GetAll safety poll — no 2s polling for a value
that changes a few times a minute. Health % and its color thresholds
(<75 / <90) are carried over from the QML pane, as is hiding the
Performance profile when power-profiles-daemon doesn't offer it.
"""
from __future__ import annotations

import shutil
import subprocess
from collections import deque

from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import Vertical, VerticalScroll
from textual.widgets import Label, Sparkline

from .app import KalinPanelApp
from .theme import SHARED_CSS
from .util import run
from .widgets import Gauge

UPOWER_BUS = "org.freedesktop.UPower"
DISPLAY_DEVICE_PATH = "/org/freedesktop/UPower/devices/DisplayDevice"
DEVICE_IFACE = "org.freedesktop.UPower.Device"
SAFETY_POLL = 30.0
RATE_SAMPLES = 90

STATE_NAMES = {
    0: "Unknown", 1: "Charging", 2: "Discharging", 3: "Empty",
    4: "Fully charged", 5: "Pending charge", 6: "Pending discharge",
}
STATE_CHARGING, STATE_DISCHARGING, STATE_FULL = 1, 2, 4


def format_time(secs: int) -> str:
    if not secs or secs <= 0:
        return "--"
    hours, minutes = divmod(secs // 60, 60)
    return f"{hours}h {minutes}m" if hours else f"{minutes}m"


def read_profiles() -> tuple[list[str], str] | None:
    """([profile, ...], active) or None when power-profiles-daemon is absent."""
    if not shutil.which("powerprofilesctl"):
        return None
    try:
        out = run(["powerprofilesctl", "list"])
    except (RuntimeError, subprocess.TimeoutExpired):
        return None
    profiles: list[str] = []
    active = ""
    for line in out.splitlines():
        line = line.strip()
        if line.endswith(":"):
            name = line.rstrip(":").lstrip("* ").strip()
            profiles.append(name)
            if line.startswith("*"):
                active = name
    return (profiles, active) if profiles else None


class BatteryPanelApp(KalinPanelApp):
    """Charge, rates, health, and power profile for the display device."""

    PANEL_TITLE = "Battery"

    CSS = SHARED_CSS + """
    #battery-scroll {
        padding: 0 1;
    }

    #bat-card, #profile-card {
        padding: 1 2;
        margin-bottom: 1;
    }

    #bat-card > Label {
        color: $text-muted;
    }

    #state-line {
        color: $foreground;
        margin-bottom: 1;
    }

    /* Quiet history strip — with a near-constant rate a Sparkline renders
       full-height everywhere, so at accent colors it reads as a giant
       amber slab next to the charge gauge. */
    #rate-spark {
        height: 1;
        margin-bottom: 1;
    }

    #rate-spark > .sparkline--min-color { color: #4a3625; }
    #rate-spark > .sparkline--max-color { color: $text-muted; }
    """

    BINDINGS = [
        Binding("1", "set_profile('0')", "Profile 1", show=False),
        Binding("2", "set_profile('1')", "Profile 2", show=False),
        Binding("3", "set_profile('2')", "Profile 3", show=False),
        Binding("p", "cycle_profile", "Profile"),
        Binding("r", "refresh_now", "Refresh"),
    ]

    def __init__(self) -> None:
        super().__init__()
        self._props_iface = None
        self._rate_history: deque[float] = deque(maxlen=RATE_SAMPLES)
        self._state: dict = {}
        self._statics: dict = {}   # Model/EnergyFullDesign from the real battery device
        self._profiles: list[str] = []

    def compose_panel(self) -> ComposeResult:
        with VerticalScroll(id="battery-scroll"):
            with Vertical(id="bat-card", classes="card"):
                yield Gauge("Charge", id="charge-gauge")
                yield Label("", id="state-line")
                yield Label("", id="rate-line")
                yield Sparkline([], id="rate-spark")
                yield Label("", id="energy-line")
                yield Label("", id="health-line")
                yield Label("", id="model-line")
            with Vertical(id="profile-card", classes="card"):
                yield Label("", id="profiles")

    def on_mount(self) -> None:
        super().on_mount()
        self.query_one("#bat-card").border_title = "battery"
        self.query_one("#profile-card").border_title = "power profile"
        self._render_profiles()
        self.run_worker(self._connect_upower())

    # ── UPower over dbus-fast ───────────────────────────────────────────────

    async def _connect_upower(self) -> None:
        # Imported here, not module-top: a missing dbus-fast (or system bus)
        # must degrade to an "unavailable" panel that can still switch power
        # profiles, not a crash.
        try:
            from dbus_fast import BusType
            from dbus_fast.aio import MessageBus

            bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
            introspection = await bus.introspect(UPOWER_BUS, DISPLAY_DEVICE_PATH)
            proxy = bus.get_proxy_object(UPOWER_BUS, DISPLAY_DEVICE_PATH, introspection)
            self._props_iface = proxy.get_interface("org.freedesktop.DBus.Properties")
            self._props_iface.on_properties_changed(self._on_properties_changed)
        except Exception as exc:
            self.set_status(f"[red]UPower unavailable:[/red] {exc}")
            self._render_device()
            return
        await self._poll_device()
        # The aggregate DisplayDevice often reports no Model/EnergyFullDesign
        # (both live on the concrete battery_BATn device) — fetch those
        # statics once from the first real battery so health% can render.
        if self._state.get("IsPresent") and self._state.get("EnergyFullDesign", 0.0) <= 0:
            try:
                await self._enrich_from_real_battery(bus)
                self._render_device()
            except Exception:
                pass  # cosmetic only — the live aggregate data still renders
        self.set_interval(SAFETY_POLL, self._safety_poll)

    async def _enrich_from_real_battery(self, bus) -> None:
        introspection = await bus.introspect(UPOWER_BUS, "/org/freedesktop/UPower")
        proxy = bus.get_proxy_object(UPOWER_BUS, "/org/freedesktop/UPower", introspection)
        upower = proxy.get_interface(UPOWER_BUS)
        for path in await upower.call_enumerate_devices():
            dev_introspection = await bus.introspect(UPOWER_BUS, path)
            dev_proxy = bus.get_proxy_object(UPOWER_BUS, path, dev_introspection)
            dev_props = dev_proxy.get_interface("org.freedesktop.DBus.Properties")
            variants = await dev_props.call_get_all(DEVICE_IFACE)
            values = {key: variant.value for key, variant in variants.items()}
            if values.get("Type") == 2 and values.get("EnergyFullDesign", 0.0) > 0:
                self._statics = {
                    "EnergyFullDesign": values["EnergyFullDesign"],
                    "Model": values.get("Model", ""),
                }
                return

    def _safety_poll(self) -> None:
        self.run_worker(self._poll_device(), exclusive=True)

    async def _poll_device(self) -> None:
        if self._props_iface is None:
            return
        try:
            variants = await self._props_iface.call_get_all(DEVICE_IFACE)
        except Exception as exc:
            self.set_status(f"[red]UPower query failed:[/red] {exc}")
            return
        self._state = {key: variant.value for key, variant in variants.items()}
        self._note_rate()
        self._render_device()
        self._render_profiles()

    def _on_properties_changed(self, iface: str, changed: dict, invalidated: list) -> None:
        if iface != DEVICE_IFACE:
            return
        for key, variant in changed.items():
            self._state[key] = variant.value
        self._note_rate()
        self._render_device()

    def _note_rate(self) -> None:
        rate = self._state.get("EnergyRate")
        if rate:
            self._rate_history.append(abs(rate))
            self.query_one("#rate-spark", Sparkline).data = list(self._rate_history)

    # ── rendering ───────────────────────────────────────────────────────────

    def _render_device(self) -> None:
        state = self._state
        present = bool(state.get("IsPresent"))
        if not state:
            self.query_one("#state-line", Label).update("UPower unavailable")
            return
        if not present:
            self.query_one("#state-line", Label).update("No battery present")
            self.set_status("no battery")
            for widget_id in ("rate-line", "energy-line", "health-line", "model-line"):
                self.query_one(f"#{widget_id}", Label).update("")
            return

        pct = round(state.get("Percentage", 0.0))
        dev_state = state.get("State", 0)
        charging = dev_state == STATE_CHARGING
        full = dev_state == STATE_FULL
        self.query_one("#charge-gauge", Gauge).update_value(pct)

        state_name = STATE_NAMES.get(dev_state, "Unknown")
        if charging:
            time_str = f" · {format_time(state.get('TimeToFull', 0))} to full"
        elif dev_state == STATE_DISCHARGING:
            time_str = f" · {format_time(state.get('TimeToEmpty', 0))} remaining"
        else:
            time_str = ""
        color = ("$success" if charging or full
                 else "$error" if pct <= 20 else "$foreground")
        self.query_one("#state-line", Label).update(f"[{color}]{state_name}[/]{time_str}")

        rate = abs(state.get("EnergyRate", 0.0))
        rate_label = "charge rate" if charging else "power draw"
        self.query_one("#rate-line", Label).update(
            f"{rate_label}  {rate:.1f} W" if not full else "")

        energy = state.get("Energy", 0.0)
        energy_full = state.get("EnergyFull", 0.0)
        self.query_one("#energy-line", Label).update(
            f"energy      {energy:.1f} / {energy_full:.1f} Wh")

        design = state.get("EnergyFullDesign", 0.0) or self._statics.get("EnergyFullDesign", 0.0)
        if design > 0 and energy_full > 0:
            health = round(energy_full / design * 100)
            # same thresholds as the old QML battery pane
            health_color = ("$warning" if health < 90 else "$success")
            if health < 75:
                health_color = "$error"
            self.query_one("#health-line", Label).update(
                f"health      [{health_color}]{health}%[/]  (design {design:.1f} Wh)")
        else:
            self.query_one("#health-line", Label).update("")

        model = state.get("Model", "") or self._statics.get("Model", "")
        self.query_one("#model-line", Label).update(f"model       {model}" if model else "")

        self.set_status(f"{state_name.lower()} · {pct}%"
                        + (f" · {rate:.1f} W" if rate and not full else ""))

    def _render_profiles(self) -> None:
        info = read_profiles()
        card = self.query_one("#profile-card")
        if info is None:
            card.display = False
            self._profiles = []
            return
        card.display = True
        self._profiles, active = info
        parts = []
        for index, name in enumerate(self._profiles):
            marker = "●" if name == active else "○"
            style = "$primary" if name == active else "$text-muted"
            parts.append(f"[{style}]{marker} {index + 1}:{name}[/]")
        self.query_one("#profiles", Label).update("   ".join(parts))

    # ── actions ─────────────────────────────────────────────────────────────

    def _set_profile(self, name: str) -> None:
        try:
            run(["powerprofilesctl", "set", name])
        except (RuntimeError, FileNotFoundError, subprocess.TimeoutExpired) as exc:
            self.set_status(f"[red]profile switch failed:[/red] {exc}")
            return
        self._render_profiles()

    def action_set_profile(self, index_str: str) -> None:
        index = int(index_str)
        if 0 <= index < len(self._profiles):
            self._set_profile(self._profiles[index])

    def action_cycle_profile(self) -> None:
        info = read_profiles()
        if not info or not info[0]:
            return
        profiles, active = info
        try:
            current = profiles.index(active)
        except ValueError:
            current = -1
        self._set_profile(profiles[(current + 1) % len(profiles)])

    def action_refresh_now(self) -> None:
        self._safety_poll()
        self._render_profiles()


def main() -> None:
    BatteryPanelApp().run()
