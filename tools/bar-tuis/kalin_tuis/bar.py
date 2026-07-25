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

Panel toggling: clicking a status group docks/undocks its kalin_tuis panel
(the foot-hosted TUIs BottomBar's DockedPanel used to own). Click-toggle
only — DockedPanel's hover-open/grace dance needs cursor-over-QML state the
bar no longer has; dock_hover-based auto-close is a possible follow-up.
KALIN_BAR_OUTPUT (set by BarHost) names this bar's output so panels land on
the right monitor; geometry comes from the "outputs" state broadcast.
"""
from __future__ import annotations

import asyncio
import glob
import json
import os
import socket
import subprocess
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

# Panel geometry — keep in sync with BarConfig.qml's tuiPanelWidth/Height
# (sized for 80x24 terminal cells; btop refuses smaller) and barHeight.
PANEL_W, PANEL_H, BAR_H = 700, 480, 22

BAR_OUTPUT = os.environ.get("KALIN_BAR_OUTPUT", "")

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


# Toggleable panels: group key -> (appid segment, kalin-bar-tui name, glyph).
# The appid segments match what BottomBar's DockedPanels used
# (kalin-<seg>-panel-<output>) so panel instances spawned before the cutover
# keep being reused after it.
PANEL_DEFS = {
    "stats":   ("stats",   "stats",     ""),
    "battery": ("battery", "battery",   "󰁹"),
    "volume":  ("volume",  "mixer",     "󰕾"),
    "wifi":    ("wifi",    "wifi",      "󰖩"),
    "bt":      ("bt",      "bluetooth", "󰂯"),
    "disk":    ("disk",    "disk",      ""),
    "display": ("display", "display",   "󰍹"),
    "clip":    ("clip",    "clipboard", "󰅌"),
}


class PanelGroup(Static):
    """One right-side ⟨ ⟩ group; click toggles its docked panel TUI. Body
    markup (meters/values) is pushed by the app's pollers; glyph-only groups
    (wifi/bt/...) just show their icon."""

    def __init__(self, key: str, widget_id: str) -> None:
        super().__init__("", id=widget_id, classes="cell")
        self.key = key
        self.body = ""

    def set_body(self, body: str) -> None:
        self.body = body
        self.refresh_markup()

    def refresh_markup(self) -> None:
        glyph = PANEL_DEFS[self.key][2]
        hot = self.app.open_panel == self.key  # type: ignore[attr-defined]
        inner = f"{self.body} " if self.body else ""
        bracket = f"bold {AMBER}" if hot else EDGE
        self.update(
            f"[{bracket}]⟨[/] [{LABEL}]{glyph}[/] {inner}[{bracket}]⟩[/]" + SEP)

    def on_click(self) -> None:
        if os.environ.get("KALIN_BAR_DEBUG"):
            with open("/tmp/bar-click.log", "a") as fh:
                fh.write(f"panel group click: {self.key}\n")
        self.app.toggle_panel(self.key)  # type: ignore[attr-defined]


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
        # is no room for underlines in a single-row bar next to a 1-row icon.
        # #4a3625 (Theme.qml border): surfaceActive was too subtle against
        # the bar ground at this size.
        self.styles.background = "#4a3625" if self.focused_flag else None

    def on_click(self) -> None:
        ipc_send(f"focus {self.client_id}")


class BarApp(App):
    ENABLE_COMMAND_PALETTE = False

    CSS = """
    Screen { background: #1e1915; }
    #row { height: 100%; align-vertical: middle; }
    #taskbar { width: auto; height: 1; }
    .taskbar-entry { width: 4; height: 1; padding: 0 1; }
    .task-icon { width: 2; height: 1; }
    .task-glyph { width: 2; height: 1; content-align: center middle; }
    .cell { width: auto; height: 1; }
    #pad { width: 1fr; height: 1; }
    """

    def compose(self) -> ComposeResult:
        with Horizontal(id="row"):
            yield Horizontal(id="taskbar")
            yield Static("", id="pad")
            yield PanelGroup("stats", "stats")
            yield PanelGroup("battery", "battery")
            yield PanelGroup("volume", "volume")
            yield PanelGroup("wifi", "wifi")
            yield PanelGroup("bt", "bt")
            yield PanelGroup("disk", "disk")
            yield PanelGroup("display", "display")
            yield PanelGroup("clip", "clip")
            yield Static("", id="clock", classes="cell")

    def on_mount(self) -> None:
        self.register_theme(KALIN_THEME)
        self.theme = KALIN_THEME.name
        self._taskbar_sig: tuple | None = None
        self.outputs: list[dict] = []
        self.open_panel: str | None = None
        self._panel_procs: dict[str, subprocess.Popen] = {}
        for g in self.query(PanelGroup):
            g.refresh_markup()
        self.update_clock()
        self.set_interval(1, self.update_clock)
        self.update_slow()
        self.set_interval(2, self.update_slow)
        self.update_battery()
        self.set_interval(30, self.update_battery)
        self.run_worker(self.ipc_stream(), exclusive=True)

    # ── docked panel toggling ────────────────────────────────────────────
    def _own_output(self) -> dict | None:
        for o in self.outputs:
            if o.get("name") == BAR_OUTPUT:
                return o
        return self.outputs[0] if self.outputs else None

    def toggle_panel(self, key: str) -> None:
        prev = self.open_panel
        if prev:
            seg = PANEL_DEFS[prev][0]
            appid = f"kalin-{seg}-panel-{BAR_OUTPUT}"
            ipc_send(f"undock {appid}")
            ipc_send(f"minimize {appid} 1")
            self.open_panel = None
        if key != prev:
            self._open_panel(key)
        for g in self.query(PanelGroup):
            g.refresh_markup()

    def _open_panel(self, key: str) -> None:
        out = self._own_output()
        if not out:
            return
        seg, tui, _ = PANEL_DEFS[key]
        appid = f"kalin-{seg}-panel-{BAR_OUTPUT}"
        x = out["x"] + out["width"] - PANEL_W
        y = out["y"] + out["height"] - BAR_H - PANEL_H
        rect = f"{x} {y} {PANEL_W} {PANEL_H}"
        proc = self._panel_procs.get(key)
        if proc is None or proc.poll() is not None:
            # dockprep BEFORE spawning: the panel's first frame is already
            # docked, never a flash at a floating position (same dance as
            # DockedPanel.qml / BarHost).
            ipc_send(f"dockprep {appid} {rect}")
            try:
                self._panel_procs[key] = subprocess.Popen(
                    ["foot", f"--app-id={appid}", "-e", "kalin-bar-tui", tui],
                    start_new_session=True,
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except OSError:
                return  # foot/kalin-bar-tui missing; leave the group cold
        else:
            ipc_send(f"minimize {appid} 0")
            ipc_send(f"dock {appid} {rect}")
        self.open_panel = key

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
        self.outputs = state.get("outputs") or self.outputs
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
            self.query_one("#stats", PanelGroup).set_body(
                f"{meter(cpu)} [bold {TEXT}]{cpu:3.0f}%[/]"
                f" [{LABEL}]󰧑[/] {meter(mem)} [bold {TEXT}]{mem:3.0f}%[/]")
        except Exception:
            self.query_one("#stats", PanelGroup).set_body("")
        try:
            out = run(["wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"], timeout=2)
            # "Volume: 0.65 [MUTED]" — cube already applied by wpctl
            vol = float(out.split()[1]) * 100
            muted = "MUTED" in out
            self.query_one("#volume", PanelGroup).set_body(
                f"[bold {ERROR}]mute[/]" if muted
                else f"{meter(vol)} [bold {TEXT}]{vol:3.0f}%[/]")
        except Exception:
            self.query_one("#volume", PanelGroup).set_body("")

    def update_battery(self) -> None:
        try:
            caps = glob.glob("/sys/class/power_supply/BAT*/capacity")
            if not caps:
                self.query_one("#battery", PanelGroup).set_body("")
                return
            base = os.path.dirname(caps[0])
            pct = float(open(caps[0]).read().strip())
            status = open(os.path.join(base, "status")).read().strip()
            charging = status in ("Charging", "Full")
            color = ERROR if (pct <= 20 and not charging) else TEXT
            self.query_one("#battery", PanelGroup).set_body(
                f"{meter(pct)} [bold {color}]{pct:3.0f}%[/]")
        except OSError:
            self.query_one("#battery", PanelGroup).set_body("")


def main() -> None:
    BarApp().run()
