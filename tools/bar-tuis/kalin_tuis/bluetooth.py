"""Bluetooth panel — BlueZ-over-D-Bus bluetuith replacement.

dbus-fast (async, shares Textual's event loop) instead of scraping
bluetoothctl: bluetoothctl is an interactive REPL, not a CLI. Device state
comes from ObjectManager.GetManagedObjects on a 3s poll plus
InterfacesAdded/Removed signals — per-device PropertiesChanged plumbing
isn't worth the code for a panel whose one call every 3s is answered from
bluezd's memory.

Pairing: a minimal Agent1 (KeyboardDisplay) is registered as default —
RequestConfirmation/RequestAuthorization get a y/n modal, pin/passkey
requests get an input modal, display-passkey lands in the status line.
Exotic pairing flows: use bluetoothctl in a normal terminal.
"""
from __future__ import annotations

import asyncio

from rich.text import Text

from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import Vertical
from textual.screen import ModalScreen
from textual.widgets import Input, Label, OptionList
from textual.widgets.option_list import Option

from .app import KalinPanelApp
from .theme import KALIN_THEME, SHARED_CSS, TEXT_MUTED_HEX

BLUEZ_BUS = "org.bluez"
POLL_INTERVAL = 3.0
AGENT_PATH = "/kalin/agent"

DEVICE_ICONS = {
    "audio-headset": "󰋋", "audio-headphones": "󰋋", "audio-card": "󰦧",
    "input-keyboard": "󰌌", "input-mouse": "󰍽", "input-gaming": "󰊗",
    "phone": "󰏲", "computer": "󰇅", "video-display": "󰍹",
}
DEFAULT_ICON = "󰂯"


class Device:
    def __init__(self, path: str, props: dict) -> None:
        self.path = path
        self.address: str = props.get("Address", "?")
        self.alias: str = props.get("Alias", self.address)
        self.icon: str = DEVICE_ICONS.get(props.get("Icon", ""), DEFAULT_ICON)
        self.paired: bool = bool(props.get("Paired"))
        self.trusted: bool = bool(props.get("Trusted"))
        self.connected: bool = bool(props.get("Connected"))
        self.rssi: int | None = props.get("RSSI")

    def snapshot(self) -> tuple:
        return (self.path, self.alias, self.paired, self.trusted,
                self.connected, self.rssi)


def device_row(dev: Device) -> Text:
    badges = []
    if dev.connected:
        badges.append(("connected", str(KALIN_THEME.success)))
    elif dev.paired:
        badges.append(("paired", TEXT_MUTED_HEX))
    if dev.trusted:
        badges.append(("trusted", TEXT_MUTED_HEX))
    row = Text.assemble(
        (f"{dev.icon} ", KALIN_THEME.primary if dev.connected else TEXT_MUTED_HEX),
        (f"{dev.alias[:30]:<30}", "" if dev.paired or dev.connected else TEXT_MUTED_HEX),
        (f"{dev.address}  ", TEXT_MUTED_HEX),
    )
    for text, style in badges:
        row.append(f"{text} ", style)
    if dev.rssi is not None:
        row.append(f" {dev.rssi} dBm", TEXT_MUTED_HEX)
    return row


class ConfirmScreen(ModalScreen[bool]):
    """y/n pairing confirmation."""

    BINDINGS = [
        Binding("y", "dismiss(True)", "Yes", priority=True),
        Binding("n,escape", "dismiss(False)", "No", priority=True),
    ]

    def __init__(self, prompt: str) -> None:
        super().__init__()
        self._prompt = prompt

    def compose(self) -> ComposeResult:
        with Vertical(classes="card modal-card"):
            yield Label(self._prompt)
            yield Label("y / n", classes="modal-hint")


class TextPromptScreen(ModalScreen[str | None]):
    """PIN/passkey entry during legacy pairing."""

    BINDINGS = [Binding("escape", "dismiss(None)", "Cancel", priority=True)]

    def __init__(self, prompt: str) -> None:
        super().__init__()
        self._prompt = prompt

    def compose(self) -> ComposeResult:
        with Vertical(classes="card modal-card"):
            yield Label(self._prompt)
            yield Input(id="pairing-input")

    def on_mount(self) -> None:
        self.query_one("#pairing-input", Input).focus()

    def on_input_submitted(self, event: Input.Submitted) -> None:
        self.dismiss(event.value or None)


class BluetoothPanelApp(KalinPanelApp):
    """Adapter + device list with pair/connect/trust/remove/scan."""

    PANEL_TITLE = "Bluetooth"

    CSS = SHARED_CSS + """
    #devices {
        height: 1fr;
        border: none;
        padding: 0 1;
        scrollbar-size-vertical: 1;
    }

    ConfirmScreen, TextPromptScreen {
        align: center middle;
    }

    .modal-card {
        width: 56;
        padding: 1 2;
        background: $panel;
    }

    .modal-hint {
        color: $text-muted;
        margin-top: 1;
    }

    .modal-card > Input {
        border: round #4a3625;
        margin-top: 1;
    }

    .modal-card > Input:focus {
        border: round $primary;
    }
    """

    BINDINGS = [
        # enter is footer display; live path = on_option_list_option_selected.
        Binding("enter", "toggle_connect", "Connect/disc."),
        Binding("p", "pair", "Pair"),
        Binding("t", "toggle_trust", "Trust"),
        Binding("x", "remove", "Remove"),
        Binding("s", "toggle_scan", "Scan"),
        Binding("w", "toggle_power", "Power"),
        Binding("r", "refresh_now", "Refresh"),
    ]

    def __init__(self) -> None:
        super().__init__()
        self._bus = None
        self._object_manager = None
        self._adapter_path = ""
        self._devices: list[Device] = []
        self._adapter_props: dict = {}
        self._last_snapshot: tuple | None = None
        self._busy = False

    def compose_panel(self) -> ComposeResult:
        yield OptionList(id="devices")

    def on_mount(self) -> None:
        super().on_mount()
        self.query_one("#devices", OptionList).focus()
        self.run_worker(self._connect_bluez())

    # ── BlueZ plumbing ──────────────────────────────────────────────────────

    async def _connect_bluez(self) -> None:
        try:
            from dbus_fast import BusType
            from dbus_fast.aio import MessageBus

            self._bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
            introspection = await self._bus.introspect(BLUEZ_BUS, "/")
            proxy = self._bus.get_proxy_object(BLUEZ_BUS, "/", introspection)
            self._object_manager = proxy.get_interface("org.freedesktop.DBus.ObjectManager")
            self._object_manager.on_interfaces_added(self._on_interfaces_changed)
            self._object_manager.on_interfaces_removed(self._on_interfaces_changed)
        except Exception as exc:
            self.set_status(f"[red]BlueZ unavailable:[/red] {exc}")
            return
        await self._register_agent()
        await self._poll()
        self.set_interval(POLL_INTERVAL, self._schedule_poll)

    async def _register_agent(self) -> None:
        try:
            from dbus_fast.service import ServiceInterface, method

            app = self

            class PairingAgent(ServiceInterface):
                def __init__(self) -> None:
                    super().__init__("org.bluez.Agent1")

                def _reject_unless(self, accepted: bool):
                    from dbus_fast import DBusError
                    if not accepted:
                        raise DBusError("org.bluez.Error.Rejected", "rejected by user")

                @method()
                async def RequestConfirmation(self, device: "o", passkey: "u"):
                    self._reject_unless(await app.modal_bool(
                        f"confirm passkey [$primary]{passkey:06d}[/] ?"))

                @method()
                async def RequestAuthorization(self, device: "o"):
                    self._reject_unless(await app.modal_bool("authorize pairing?"))

                @method()
                def AuthorizeService(self, device: "o", uuid: "s"):
                    pass  # paired devices may use any profile

                @method()
                async def RequestPinCode(self, device: "o") -> "s":
                    pin = await app.modal_text("PIN code:")
                    self._reject_unless(pin is not None)
                    return pin

                @method()
                async def RequestPasskey(self, device: "o") -> "u":
                    passkey = await app.modal_text("passkey (digits):")
                    self._reject_unless(passkey is not None and passkey.isdigit())
                    return int(passkey)

                @method()
                def DisplayPasskey(self, device: "o", passkey: "u", entered: "q"):
                    app.set_status(f"enter passkey on device: [$primary]{passkey:06d}[/]")

                @method()
                def DisplayPinCode(self, device: "o", pincode: "s"):
                    app.set_status(f"enter PIN on device: [$primary]{pincode}[/]")

                @method()
                def RequestCancel(self):
                    pass

                @method()
                def Release(self):
                    pass

            self._bus.export(AGENT_PATH, PairingAgent())
            introspection = await self._bus.introspect(BLUEZ_BUS, "/org/bluez")
            proxy = self._bus.get_proxy_object(BLUEZ_BUS, "/org/bluez", introspection)
            manager = proxy.get_interface("org.bluez.AgentManager1")
            await manager.call_register_agent(AGENT_PATH, "KeyboardDisplay")
            await manager.call_request_default_agent(AGENT_PATH)
        except Exception as exc:
            # Pairing simple devices still works agent-less (Just-Works).
            self.set_status(f"[$warning]no pairing agent:[/] {exc}")

    def modal_bool(self, prompt: str) -> asyncio.Future:
        future = asyncio.get_event_loop().create_future()
        self.push_screen(ConfirmScreen(prompt),
                         lambda result: future.set_result(bool(result)))
        return future

    def modal_text(self, prompt: str) -> asyncio.Future:
        future = asyncio.get_event_loop().create_future()
        self.push_screen(TextPromptScreen(prompt),
                         lambda result: future.set_result(result))
        return future

    def _on_interfaces_changed(self, path: str, interfaces) -> None:
        self._schedule_poll()

    def _schedule_poll(self) -> None:
        self.run_worker(self._poll(), exclusive=True, group="bt-poll")

    async def _poll(self) -> None:
        if self._object_manager is None:
            return
        try:
            objects = await self._object_manager.call_get_managed_objects()
        except Exception as exc:
            self.set_status(f"[red]BlueZ query failed:[/red] {exc}")
            return

        self._adapter_path = ""
        self._adapter_props = {}
        devices: list[Device] = []
        for path, interfaces in objects.items():
            if "org.bluez.Adapter1" in interfaces and not self._adapter_path:
                self._adapter_path = path
                self._adapter_props = {
                    key: variant.value
                    for key, variant in interfaces["org.bluez.Adapter1"].items()}
            elif "org.bluez.Device1" in interfaces:
                props = {key: variant.value
                         for key, variant in interfaces["org.bluez.Device1"].items()}
                if props.get("Name") or props.get("Paired") or props.get("RSSI") is not None:
                    devices.append(Device(path, props))
        devices.sort(key=lambda d: (not d.connected, not d.paired,
                                    -(d.rssi or -999), d.alias.lower()))
        self._devices = devices
        self._render()

    def _render(self) -> None:
        snapshot = (tuple(sorted(self._adapter_props.get(k, False) is True
                                 for k in ("Powered", "Discovering"))),
                    tuple(dev.snapshot() for dev in self._devices))
        if snapshot == self._last_snapshot:
            return
        self._last_snapshot = snapshot

        devices_list = self.query_one("#devices", OptionList)
        old_highlight = devices_list.highlighted
        devices_list.clear_options()
        for dev in self._devices:
            devices_list.add_option(Option(device_row(dev)))
        if self._devices:
            devices_list.highlighted = min(old_highlight or 0, len(self._devices) - 1)

        if not self._adapter_path:
            self.set_status("[red]no bluetooth adapter[/red]")
            return
        powered = self._adapter_props.get("Powered", False)
        discovering = self._adapter_props.get("Discovering", False)
        connected = sum(1 for d in self._devices if d.connected)
        self.set_status(
            ("[$success]on[/]" if powered else "[$warning]off[/] — w to power on")
            + (" · [$primary]scanning…[/]" if discovering else "")
            + f" · {connected} connected · {len(self._devices)} devices")

    # ── device/adapter proxies ──────────────────────────────────────────────

    async def _interface(self, path: str, name: str):
        introspection = await self._bus.introspect(BLUEZ_BUS, path)
        proxy = self._bus.get_proxy_object(BLUEZ_BUS, path, introspection)
        return proxy.get_interface(name)

    def _selected(self) -> Device | None:
        idx = self.query_one("#devices", OptionList).highlighted
        if idx is None or not (0 <= idx < len(self._devices)):
            return None
        return self._devices[idx]

    def _start(self, label: str, coro) -> None:
        if self._busy:
            return
        self._busy = True
        self.set_status(f"{label}…")
        self.run_worker(self._action_worker(label, coro), exclusive=True,
                        group="bt-action")

    async def _action_worker(self, label: str, coro) -> None:
        try:
            await coro
        except Exception as exc:
            self.set_status(f"[red]{label} failed:[/red] {exc}")
            return
        finally:
            self._busy = False
        await self._poll()

    # ── actions ─────────────────────────────────────────────────────────────

    def on_option_list_option_selected(self, event: OptionList.OptionSelected) -> None:
        self.action_toggle_connect()

    def action_toggle_connect(self) -> None:
        dev = self._selected()
        if dev is None:
            return

        async def toggle() -> None:
            device = await self._interface(dev.path, "org.bluez.Device1")
            if dev.connected:
                await device.call_disconnect()
            else:
                await device.call_connect()

        self._start("disconnecting" if dev.connected else f"connecting {dev.alias}", toggle())

    def action_pair(self) -> None:
        dev = self._selected()
        if dev is None or dev.paired:
            return

        async def pair() -> None:
            device = await self._interface(dev.path, "org.bluez.Device1")
            await asyncio.wait_for(device.call_pair(), timeout=60)

        self._start(f"pairing {dev.alias}", pair())

    def action_toggle_trust(self) -> None:
        dev = self._selected()
        if dev is None:
            return

        async def trust() -> None:
            from dbus_fast import Variant
            props = await self._interface(dev.path, "org.freedesktop.DBus.Properties")
            await props.call_set("org.bluez.Device1", "Trusted",
                                 Variant("b", not dev.trusted))

        self._start("updating trust", trust())

    def action_remove(self) -> None:
        dev = self._selected()
        if dev is None or not self._adapter_path:
            return

        async def remove() -> None:
            adapter = await self._interface(self._adapter_path, "org.bluez.Adapter1")
            await adapter.call_remove_device(dev.path)

        self._start(f"removing {dev.alias}", remove())

    def action_toggle_scan(self) -> None:
        if not self._adapter_path:
            return
        discovering = self._adapter_props.get("Discovering", False)

        async def scan() -> None:
            adapter = await self._interface(self._adapter_path, "org.bluez.Adapter1")
            if discovering:
                await adapter.call_stop_discovery()
            else:
                await adapter.call_start_discovery()

        self._start("stopping scan" if discovering else "starting scan", scan())

    def action_toggle_power(self) -> None:
        if not self._adapter_path:
            return
        powered = self._adapter_props.get("Powered", False)

        async def power() -> None:
            from dbus_fast import Variant
            props = await self._interface(self._adapter_path,
                                          "org.freedesktop.DBus.Properties")
            await props.call_set("org.bluez.Adapter1", "Powered",
                                 Variant("b", not powered))

        self._start("powering off" if powered else "powering on", power())

    def action_refresh_now(self) -> None:
        self._last_snapshot = None
        self._schedule_poll()


def main() -> None:
    BluetoothPanelApp().run()
