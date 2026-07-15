"""WiFi panel — nmcli-based nmtui replacement for the docked bar.

All reads go through `nmcli -t` (terse, colon-separated with `\\:` escaping
— parsed by split_terse(), never a naive split(':')). Writes go through
nmcli too; connect runs on run_async so a slow association can't freeze the
UI. Passwords travel via `nmcli dev wifi connect ... password ...` argv —
acceptable on a single-user laptop, same exposure window as running it in a
shell.

Scan model: the 15s poll always uses --rescan no (a forced scan bounces the
radio and stalls traffic); an explicit `s` triggers one async rescan.
"""
from __future__ import annotations

import asyncio
import subprocess
from dataclasses import dataclass

from rich.text import Text

from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import Vertical
from textual.screen import ModalScreen
from textual.widgets import Input, Label, OptionList
from textual.widgets.option_list import Option

from .app import KalinPanelApp
from .theme import BORDER_HEX, KALIN_THEME, SHARED_CSS, TEXT_MUTED_HEX
from .util import run, run_async

POLL_INTERVAL = 15.0
SIGNAL_BLOCKS = "▂▄▆█"
SECRET_ERRORS = ("Secrets were required", "passwords or encryption keys are required")


def split_terse(line: str) -> list[str]:
    """Split one `nmcli -t` line on unescaped colons ('\\:' and '\\\\' are
    escapes inside values — SSIDs can contain both)."""
    fields: list[str] = []
    current: list[str] = []
    escaped = False
    for char in line:
        if escaped:
            current.append(char)
            escaped = False
        elif char == "\\":
            escaped = True
        elif char == ":":
            fields.append("".join(current))
            current = []
        else:
            current.append(char)
    fields.append("".join(current))
    return fields


@dataclass
class Network:
    ssid: str
    signal: int
    security: str      # "" = open
    in_use: bool
    saved: bool


def wifi_device() -> tuple[str, str]:
    """(device, state) of the first wifi interface, or ("", "")."""
    for line in run(["nmcli", "-t", "-f", "DEVICE,TYPE,STATE", "dev", "status"]).splitlines():
        fields = split_terse(line)
        if len(fields) >= 3 and fields[1] == "wifi":
            return fields[0], fields[2]
    return "", ""


def saved_wifi_names() -> set[str]:
    names = set()
    for line in run(["nmcli", "-t", "-f", "NAME,TYPE", "connection", "show"]).splitlines():
        fields = split_terse(line)
        if len(fields) >= 2 and "wireless" in fields[1]:
            names.add(fields[0])
    return names


def scan_networks(saved: set[str]) -> list[Network]:
    """Visible networks, deduped by SSID (strongest AP wins)."""
    by_ssid: dict[str, Network] = {}
    out = run(["nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY",
               "dev", "wifi", "list", "--rescan", "no"])
    for line in out.splitlines():
        fields = split_terse(line)
        if len(fields) < 4 or not fields[1]:
            continue  # hidden SSID
        in_use = fields[0] == "*"
        ssid = fields[1]
        try:
            signal = int(fields[2])
        except ValueError:
            signal = 0
        security = "" if fields[3] in ("", "--") else fields[3]
        existing = by_ssid.get(ssid)
        if existing is None or signal > existing.signal or in_use:
            by_ssid[ssid] = Network(ssid, max(signal, existing.signal if existing else 0),
                                    security, in_use or (existing.in_use if existing else False),
                                    ssid in saved)
    networks = list(by_ssid.values())
    networks.sort(key=lambda n: (not n.in_use, not n.saved, -n.signal, n.ssid.lower()))
    return networks


def device_ip(device: str) -> str:
    try:
        for line in run(["nmcli", "-t", "-f", "IP4.ADDRESS", "dev", "show", device]).splitlines():
            _, _, value = line.partition(":")
            if value:
                return value.split("/")[0]
    except RuntimeError:
        pass
    return ""


def network_row(net: Network) -> Text:
    bars = min(3, net.signal // 25)
    return Text.assemble(
        ("✓ " if net.in_use else "  ", KALIN_THEME.success),
        (SIGNAL_BLOCKS[:bars + 1], KALIN_THEME.primary),
        (SIGNAL_BLOCKS[bars + 1:], BORDER_HEX),
        "  ",
        (f"{net.ssid[:38]:<38}", "" if net.in_use or net.saved else TEXT_MUTED_HEX),
        ("󰌾 " if net.security else "  ", TEXT_MUTED_HEX),
        (" saved" if net.saved and not net.in_use else "", TEXT_MUTED_HEX),
    )


class PasswordScreen(ModalScreen[str | None]):
    """Passphrase prompt for a new secured network; dismisses with None on
    escape, the passphrase on enter."""

    BINDINGS = [Binding("escape", "dismiss(None)", "Cancel", priority=True)]

    def __init__(self, ssid: str, error: str = "") -> None:
        super().__init__()
        self._ssid = ssid
        self._error = error

    def compose(self) -> ComposeResult:
        with Vertical(classes="card modal-card"):
            yield Label(f"passphrase for [$primary]{self._ssid}[/]")
            if self._error:
                yield Label(self._error, classes="modal-error")
            yield Input(password=True, id="passphrase")

    def on_mount(self) -> None:
        self.query_one("#passphrase", Input).focus()

    def on_input_submitted(self, event: Input.Submitted) -> None:
        self.dismiss(event.value or None)


class WifiPanelApp(KalinPanelApp):
    """Network list + connect/disconnect/forget/rescan/radio-toggle."""

    PANEL_TITLE = "WiFi"

    CSS = SHARED_CSS + """
    #networks {
        height: 1fr;
        border: none;
        padding: 0 1;
        scrollbar-size-vertical: 1;
    }

    PasswordScreen {
        align: center middle;
    }

    .modal-card {
        width: 56;
        padding: 1 2;
        background: $panel;
    }

    .modal-error {
        color: $error;
    }

    .modal-card > Input {
        border: round #4a3625;
        margin-top: 1;
    }

    .modal-card > Input:focus {
        border: round $primary;
    }
    """

    # `enter` is listed for the footer, but the live path is
    # on_option_list_option_selected: a focused OptionList consumes enter
    # itself (so the binding can't fire), and a priority binding would
    # steal enter from the password modal's Input.
    BINDINGS = [
        Binding("enter", "connect", "Connect"),
        Binding("d", "disconnect", "Disconnect"),
        Binding("f", "forget", "Forget"),
        Binding("s", "rescan", "Rescan"),
        Binding("t", "toggle_radio", "Radio on/off"),
        Binding("r", "refresh_now", "Refresh"),
    ]

    def __init__(self) -> None:
        super().__init__()
        self._networks: list[Network] = []
        self._device = ""
        self._busy = False   # one connect/rescan at a time

    def compose_panel(self) -> ComposeResult:
        yield OptionList(id="networks")

    def on_mount(self) -> None:
        super().on_mount()
        self.query_one("#networks", OptionList).focus()
        self.start_poll(POLL_INTERVAL, self._refresh)

    def _selected(self) -> Network | None:
        idx = self.query_one("#networks", OptionList).highlighted
        if idx is None or not (0 <= idx < len(self._networks)):
            return None
        return self._networks[idx]

    def _refresh(self) -> None:
        try:
            radio_on = run(["nmcli", "-t", "radio", "wifi"]).strip() == "enabled"
            self._device, dev_state = wifi_device()
            saved = saved_wifi_names()
            self._networks = scan_networks(saved) if radio_on else []
        except (RuntimeError, FileNotFoundError, subprocess.TimeoutExpired) as exc:
            self.set_status(f"[red]nmcli failed:[/red] {exc}")
            return

        networks_list = self.query_one("#networks", OptionList)
        old_highlight = networks_list.highlighted
        networks_list.clear_options()
        for net in self._networks:
            networks_list.add_option(Option(network_row(net)))
        if self._networks:
            networks_list.highlighted = min(old_highlight or 0, len(self._networks) - 1)

        if not radio_on:
            self.set_status("[$warning]radio off[/] — t to enable")
            return
        active = next((n for n in self._networks if n.in_use), None)
        if active:
            ip = device_ip(self._device) if self._device else ""
            self.set_status(f"[$success]{active.ssid}[/]"
                            + (f" · {ip}" if ip else "") + f" · {active.signal}%")
        else:
            self.set_status(f"not connected · {len(self._networks)} networks")

    # ── actions ─────────────────────────────────────────────────────────────

    def on_option_list_option_selected(self, event: OptionList.OptionSelected) -> None:
        self.action_connect()

    def action_connect(self) -> None:
        net = self._selected()
        if net is None or net.in_use or self._busy:
            return
        if net.saved:
            self._start_connect(["nmcli", "con", "up", net.ssid], net.ssid)
        elif not net.security:
            self._start_connect(["nmcli", "dev", "wifi", "connect", net.ssid], net.ssid)
        else:
            self._ask_password(net.ssid)

    def _ask_password(self, ssid: str, error: str = "") -> None:
        def on_result(passphrase: str | None) -> None:
            if passphrase:
                self._start_connect(
                    ["nmcli", "dev", "wifi", "connect", ssid, "password", passphrase],
                    ssid, new_secured=True)
        self.push_screen(PasswordScreen(ssid, error), on_result)

    def _start_connect(self, argv: list[str], ssid: str, new_secured: bool = False) -> None:
        self._busy = True
        self.set_status(f"connecting to {ssid}…")
        self.run_worker(self._connect_worker(argv, ssid, new_secured), exclusive=True)

    async def _connect_worker(self, argv: list[str], ssid: str, new_secured: bool) -> None:
        try:
            await run_async(argv)
        except RuntimeError as exc:
            message = str(exc)
            if any(marker in message for marker in SECRET_ERRORS):
                # A failed first attempt leaves a half-created profile behind
                # that would shadow the next try as "saved" — drop it, then
                # re-prompt with the error visible.
                try:
                    await run_async(["nmcli", "connection", "delete", ssid], timeout=10)
                except RuntimeError:
                    pass
                self._ask_password(ssid, "wrong passphrase?")
            self.set_status(f"[red]connect failed:[/red] {message[:80]}")
            return
        finally:
            self._busy = False
        self._refresh()

    def action_disconnect(self) -> None:
        if not self._device:
            return
        try:
            run(["nmcli", "dev", "disconnect", self._device], timeout=15)
        except (RuntimeError, subprocess.TimeoutExpired) as exc:
            self.set_status(f"[red]disconnect failed:[/red] {exc}")
            return
        self._refresh()

    def action_forget(self) -> None:
        net = self._selected()
        if net is None or not net.saved:
            return
        try:
            run(["nmcli", "connection", "delete", net.ssid], timeout=15)
        except (RuntimeError, subprocess.TimeoutExpired) as exc:
            self.set_status(f"[red]forget failed:[/red] {exc}")
            return
        self.set_status(f"forgot {net.ssid}")
        self._refresh()

    def action_rescan(self) -> None:
        if self._busy:
            return
        self._busy = True
        self.set_status("scanning…")
        self.run_worker(self._rescan_worker(), exclusive=True)

    async def _rescan_worker(self) -> None:
        try:
            await run_async(["nmcli", "dev", "wifi", "rescan"], timeout=20)
            await asyncio.sleep(2)  # give the scan results a beat to land
        except RuntimeError as exc:
            self.set_status(f"[red]rescan failed:[/red] {exc}")
        finally:
            self._busy = False
        self._refresh()

    def action_toggle_radio(self) -> None:
        try:
            enabled = run(["nmcli", "-t", "radio", "wifi"]).strip() == "enabled"
            run(["nmcli", "radio", "wifi", "off" if enabled else "on"], timeout=10)
        except (RuntimeError, subprocess.TimeoutExpired) as exc:
            self.set_status(f"[red]radio toggle failed:[/red] {exc}")
            return
        self._refresh()

    def action_refresh_now(self) -> None:
        self._refresh()


def main() -> None:
    WifiPanelApp().run()
