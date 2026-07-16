"""kalin-bar-tui bar — the bottom bar itself as a Textual TUI.

Replaces the QML BottomBar: hosted in kitty (--class kalin-bar-<output>),
docked into a strip the shell's BarHost.qml reserves (exclusive zone) and
positions via the compositor dock IPC. kitty rather than foot because the
taskbar renders real app icons through the kitty graphics protocol
(textual-image TGPImage); foot+sixel corrupts rows under Textual and ghostty
needs OpenGL 4.3 this host doesn't have — see obsidian/app-launcher.md and
the bar-restyle session notes.

Look: the "typeset statusline" direction — pure text, ⟨ ⟩ bracket groups,
│ separators, glyph meters, ─┤ clock ├─, bold values (B's type in C's frame).

Data:
  - taskbar: the compositor IPC "clients" broadcast (a TUI can't speak
    foreign-toplevel); click sends "focus <id>" (unminimize+focus+center).
  - battery /sys, volume wpctl, cpu/mem psutil — cheap polls, matching the
    backend table in obsidian/bar-tuis.md.
"""
from __future__ import annotations

import asyncio
import glob
import json
import os
import socket
from datetime import datetime

from textual.app import App, ComposeResult
from textual.containers import Horizontal
from textual.widgets import Static

from .theme import KALIN_THEME
from .util import KALIN_IPC_SOCKET, run

try:
    from textual_image.widget import TGPImage
except ImportError:  # not fatal: taskbar degrades to glyph/letter fallbacks
    TGPImage = None

# Typeset palette — Theme.qml tokens (see theme.py for the sync discipline).
AMBER = "#f0a030"
GOLD = "#ffcf5c"
TEXT = "#f0ddc0"
LABEL = "#b08d5f"
DIM = "#6b5642"
EDGE = "#4a3625"
ERROR = "#e0552f"

METER_GLYPHS = "▁▂▃▄▅▆▇█"

ICON_DIRS = (
    "/run/current-system/sw/share/icons/hicolor/{size}/apps/{name}.png",
    os.path.expanduser("~/.local/share/icons/hicolor/{size}/apps/{name}.png"),
    "/run/current-system/sw/share/pixmaps/{name}.png",
)
ICON_SIZES = ("48x48", "64x64", "128x128", "32x32")

# Nerd-font stand-ins when no raster icon resolves (and the universal
# fallback when textual-image is unavailable).
GLYPH_FALLBACK = {
    "foot": "",
    "footclient": "",
    "zen": "󰈹",
    "zen-browser": "󰈹",
    "btop": "",
    "kitty": "󰄛",
}


def meter(pct: float, cells: int = 3) -> str:
    """Block-glyph mini meter, amber filled cells / dim empty cells."""
    pct = max(0.0, min(100.0, pct))
    filled = pct / 100.0 * cells
    out = []
    for i in range(cells):
        level = max(0.0, min(1.0, filled - i))
        if level <= 0:
            out.append(f"[{DIM}]{METER_GLYPHS[0]}[/]")
        else:
            g = METER_GLYPHS[min(7, int(level * 7.999))]
            out.append(f"[bold {AMBER}]{g}[/]")
    return "".join(out)


def group(glyph: str, body: str, hot: bool = False) -> str:
    bracket = f"bold {AMBER}" if hot else EDGE
    return (f"[{bracket}]⟨[/] [{LABEL}]{glyph}[/] {body} [{bracket}]⟩[/]")


SEP = f" [{DIM}]│[/] "


def resolve_icon(appid: str) -> str | None:
    for name in (appid, appid.lower()):
        for size in ICON_SIZES:
            for pat in ICON_DIRS:
                p = pat.format(size=size, name=name)
                if os.path.exists(p):
                    return p
    # Last resort: a .desktop whose name matches carries an Icon= we can try.
    for d in ("/run/current-system/sw/share/applications",):
        for cand in glob.glob(f"{d}/{appid}*.desktop"):
            try:
                with open(cand, encoding="utf-8", errors="replace") as fh:
                    for line in fh:
                        if line.startswith("Icon="):
                            icon = line[5:].strip()
                            if os.path.isabs(icon) and os.path.exists(icon):
                                return icon
                            return resolve_icon(icon) if icon != appid else None
            except OSError:
                pass
    return None


def ipc_send(cmd: str) -> None:
    """Fire-and-forget command write, same shape as the kalin-dock CLI."""
    try:
        with socket.socket(socket.AF_UNIX) as s:
            s.settimeout(2)
            s.connect(KALIN_IPC_SOCKET)
            s.send((cmd + "\n").encode())
    except OSError:
        pass  # compositor gone = bar is about to die with it anyway


class TaskbarEntry(Static):
    """One running app: raster icon (TGP) or glyph fallback; click focuses."""

    def __init__(self, client_id: int, appid: str, focused: bool) -> None:
        super().__init__(classes="taskbar-entry")
        self.client_id = client_id
        self.appid = appid
        self.focused_flag = focused

    def compose(self) -> ComposeResult:
        icon = resolve_icon(self.appid) if TGPImage else None
        if icon:
            img = TGPImage(icon, classes="task-icon")
            yield img
        else:
            glyph = GLYPH_FALLBACK.get(
                self.appid.lower(), (self.appid[:1] or "?").upper())
            color = f"bold {GOLD}" if self.focused_flag else TEXT
            yield Static(f"[{color}]{glyph}[/]", classes="task-glyph")

    def on_mount(self) -> None:
        # Focus is marked by tinting the entry's ground, not chrome — there
        # is no room for underlines in a 2-row bar next to a 2-row icon.
        self.styles.background = "#3d2c1c" if self.focused_flag else None

    def on_click(self) -> None:
        ipc_send(f"focus {self.client_id}")


class BarApp(App):
    ENABLE_COMMAND_PALETTE = False

    CSS = """
    Screen { background: #1e1915; }
    #row { height: 100%; align-vertical: middle; }
    #taskbar { width: auto; height: 100%; }
    .taskbar-entry { width: 5; height: 100%; padding: 0 1; }
    .task-icon { width: 4; height: 2; }
    .task-glyph { width: 3; height: 1; content-align: center middle; }
    .cell { width: auto; height: 1; }
    #pad { width: 1fr; height: 1; }
    """

    def compose(self) -> ComposeResult:
        with Horizontal(id="row"):
            yield Horizontal(id="taskbar")
            yield Static("", id="pad")
            yield Static("", id="stats", classes="cell")
            yield Static("", id="battery", classes="cell")
            yield Static("", id="volume", classes="cell")
            yield Static("", id="clock", classes="cell")

    def on_mount(self) -> None:
        self.register_theme(KALIN_THEME)
        self.theme = KALIN_THEME.name
        self._taskbar_sig: tuple | None = None
        self.update_clock()
        self.set_interval(1, self.update_clock)
        self.update_slow()
        self.set_interval(2, self.update_slow)
        self.update_battery()
        self.set_interval(30, self.update_battery)
        self.run_worker(self.ipc_stream(), exclusive=True)

    # ── compositor state stream ──────────────────────────────────────────
    async def ipc_stream(self) -> None:
        """Long-lived read of the broadcast; reconnect forever — the bar
        outlives compositor dev-restarts (kitty keeps running; the socket
        path stays stable per display name)."""
        while True:
            try:
                reader, _ = await asyncio.open_unix_connection(KALIN_IPC_SOCKET)
                while True:
                    line = await reader.readline()
                    if not line:
                        break
                    try:
                        state = json.loads(line)
                    except ValueError:
                        continue
                    self.apply_state(state)
            except OSError:
                pass
            await asyncio.sleep(2)

    def apply_state(self, state: dict) -> None:
        clients = state.get("clients") or []
        sig = tuple((c["id"], c["appid"], c["focused"]) for c in clients)
        if sig == self._taskbar_sig:
            return  # broadcasts fire on every camera move; don't churn TGP
        self._taskbar_sig = sig
        bar = self.query_one("#taskbar", Horizontal)
        bar.remove_children()
        for c in clients:
            bar.mount(TaskbarEntry(c["id"], c["appid"], c["focused"]))

    # ── right-side groups ────────────────────────────────────────────────
    def update_clock(self) -> None:
        now = datetime.now().strftime("%H:%M")
        self.query_one("#clock", Static).update(
            f"[{EDGE}]─┤[/] [bold {GOLD}]{now}[/] [{EDGE}]├─[/] ")

    def update_slow(self) -> None:
        try:
            import psutil
            cpu = psutil.cpu_percent(interval=None)
            mem = psutil.virtual_memory().percent
            body = (f"{meter(cpu)} [bold {TEXT}]{cpu:3.0f}%[/]"
                    f" [{LABEL}]󰧑[/] {meter(mem)} [bold {TEXT}]{mem:3.0f}%[/]")
            self.query_one("#stats", Static).update(group("󰍛", body) + SEP)
        except Exception:
            self.query_one("#stats", Static).update("")
        try:
            out = run(["wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"], timeout=2)
            # "Volume: 0.65 [MUTED]" — cube already applied by wpctl
            vol = float(out.split()[1]) * 100
            muted = "MUTED" in out
            val = (f"[bold {ERROR}]mute[/]" if muted
                   else f"{meter(vol)} [bold {TEXT}]{vol:3.0f}%[/]")
            self.query_one("#volume", Static).update(group("󰕾", val) + SEP)
        except Exception:
            self.query_one("#volume", Static).update("")

    def update_battery(self) -> None:
        try:
            caps = glob.glob("/sys/class/power_supply/BAT*/capacity")
            if not caps:
                self.query_one("#battery", Static).update("")
                return
            base = os.path.dirname(caps[0])
            pct = float(open(caps[0]).read().strip())
            status = open(os.path.join(base, "status")).read().strip()
            charging = status in ("Charging", "Full")
            glyph = "󰂄" if charging else "󰁹"
            color = ERROR if (pct <= 20 and not charging) else TEXT
            body = f"{meter(pct)} [bold {color}]{pct:3.0f}%[/]"
            self.query_one("#battery", Static).update(group(glyph, body) + SEP)
        except OSError:
            self.query_one("#battery", Static).update("")


def main() -> None:
    BarApp().run()
