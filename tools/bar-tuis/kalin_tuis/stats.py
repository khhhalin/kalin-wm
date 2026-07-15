"""System stats panel — psutil-based btop replacement for the docked bar.

Deliberately not a btop clone: no process management, no per-process trees,
no per-disk breakdown — just the at-a-glance numbers worth a hover, sized
for the panel's ~87x26 cells. GPU readers are ported from the bar's own
modules/services/SystemStats.py (nvidia-smi, then AMD sysfs), detected once
at startup instead of retried every tick.
"""
from __future__ import annotations

import os
import subprocess
import time

import psutil

from textual.app import ComposeResult
from textual.containers import Horizontal, Vertical
from textual.widgets import DataTable, Label, Sparkline

from .app import KalinPanelApp
from .theme import SHARED_CSS
from .widgets import Gauge

POLL_INTERVAL = 2.0
SPARK_SAMPLES = 60          # 2 min of cpu history at the poll interval
TOP_PROCESSES = 8
CORE_BLOCKS = "▁▂▃▄▅▆▇█"


def detect_gpu() -> tuple[str, str] | None:
    """(kind, name) — probed once; None hides the GPU gauge entirely."""
    try:
        out = subprocess.check_output(
            ["nvidia-smi", "--query-gpu=name", "--format=csv,noheader"],
            text=True, timeout=1.0)
        return ("nvidia", out.strip().splitlines()[0].strip() or "NVIDIA")
    except Exception:
        pass
    for card in ("card0", "card1", "card2"):
        if os.path.exists(f"/sys/class/drm/{card}/device/gpu_busy_percent"):
            name = "AMD"
            name_path = f"/sys/class/drm/{card}/device/product_name"
            if os.path.exists(name_path):
                try:
                    with open(name_path) as f:
                        name = f.read().strip()
                except OSError:
                    pass
            return (f"amd:{card}", name)
    return None


def read_gpu_percent(kind: str) -> float:
    if kind == "nvidia":
        out = subprocess.check_output(
            ["nvidia-smi", "--query-gpu=utilization.gpu",
             "--format=csv,noheader,nounits"], text=True, timeout=1.0)
        return float(out.strip().splitlines()[0])
    card = kind.partition(":")[2]
    with open(f"/sys/class/drm/{card}/device/gpu_busy_percent") as f:
        return float(f.read().strip())


def hottest_temp() -> float | None:
    try:
        sensors = psutil.sensors_temperatures()
    except AttributeError:
        return None
    readings = [t.current for entries in sensors.values() for t in entries
                if t.current is not None]
    return max(readings) if readings else None


def fmt_rate(bytes_per_s: float) -> str:
    if bytes_per_s >= 1024 * 1024:
        return f"{bytes_per_s / (1024 * 1024):6.1f} MiB/s"
    return f"{bytes_per_s / 1024:6.1f} KiB/s"


class StatsPanelApp(KalinPanelApp):
    """cpu / mem / gpu / net / disk / top processes, read-only."""

    PANEL_TITLE = "System Stats"

    CSS = SHARED_CSS + """
    #top {
        height: auto;
    }

    #cpu-card {
        width: 55%;
        margin: 0 1;
    }

    #sys-card {
        width: 1fr;
        margin: 0 1 0 0;
    }

    #cpu-spark {
        height: 2;
        margin: 0 1;
    }

    #cpu-spark > .sparkline--min-color { color: #4a3625; }
    #cpu-spark > .sparkline--max-color { color: $primary; }

    #cores, #cpu-meta, #temp-line {
        padding: 0 1;
        color: $text-muted;
    }

    #io-card {
        height: 4;
        margin: 0 1;
    }

    #procs {
        margin: 0 1;
        height: 1fr;
        scrollbar-size-vertical: 1;
    }
    """

    BINDINGS = [("r", "refresh_now", "Refresh")]

    def __init__(self) -> None:
        super().__init__()
        self._gpu = detect_gpu()
        self._cpu_history: list[float] = []
        self._prev_net: tuple[float, object] | None = None   # (timestamp, counters)
        self._prev_disk: tuple[float, object] | None = None

    def compose_panel(self) -> ComposeResult:
        with Horizontal(id="top"):
            with Vertical(id="cpu-card", classes="card"):
                yield Gauge("CPU", id="cpu-gauge")
                yield Sparkline([], id="cpu-spark")
                yield Label("", id="cores")
                yield Label("", id="cpu-meta")
            with Vertical(id="sys-card", classes="card"):
                yield Gauge("Memory", id="mem-gauge")
                yield Gauge("Swap", id="swap-gauge")
                yield Gauge("GPU", id="gpu-gauge")
                yield Label("", id="temp-line")
        with Vertical(id="io-card", classes="card"):
            yield Label("", id="net-line")
            yield Label("", id="disk-line")
        yield DataTable(id="procs")

    def on_mount(self) -> None:
        super().on_mount()
        self.query_one("#cpu-card").border_title = "cpu"
        self.query_one("#sys-card").border_title = "memory · gpu"
        self.query_one("#io-card").border_title = "net · disk"
        if self._gpu is None:
            self.query_one("#gpu-gauge", Gauge).display = False
        table = self.query_one("#procs", DataTable)
        table.cursor_type = "none"
        table.add_columns("pid", "name", "cpu%", "mem%")
        # Prime the percent counters: psutil's first cpu_percent() call per
        # process/CPU always reports 0.0 — real values start next tick.
        psutil.cpu_percent(percpu=True)
        for proc in psutil.process_iter():
            try:
                proc.cpu_percent()
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass
        self.start_poll(POLL_INTERVAL, self._refresh)

    def action_refresh_now(self) -> None:
        self._refresh()

    def _refresh(self) -> None:
        now = time.monotonic()

        # cpu
        per_core = psutil.cpu_percent(percpu=True)
        total = sum(per_core) / len(per_core) if per_core else 0.0
        self.query_one("#cpu-gauge", Gauge).update_value(round(total))
        self._cpu_history = (self._cpu_history + [total])[-SPARK_SAMPLES:]
        self.query_one("#cpu-spark", Sparkline).data = list(self._cpu_history)
        self.query_one("#cores", Label).update(
            " ".join(CORE_BLOCKS[min(7, int(p / 12.51))] for p in per_core))
        freq = psutil.cpu_freq()
        load1, load5, load15 = os.getloadavg()
        freq_str = f"{freq.current / 1000:.2f} GHz   " if freq else ""
        self.query_one("#cpu-meta", Label).update(
            f"{freq_str}load {load1:.2f} {load5:.2f} {load15:.2f}")

        # memory / gpu / temp
        mem = psutil.virtual_memory()
        swap = psutil.swap_memory()
        self.query_one("#mem-gauge", Gauge).update_value(round(mem.percent))
        self.query_one("#swap-gauge", Gauge).update_value(round(swap.percent))
        if self._gpu is not None:
            try:
                self.query_one("#gpu-gauge", Gauge).update_value(
                    round(read_gpu_percent(self._gpu[0])))
            except Exception:
                pass  # a transient nvidia-smi hiccup shouldn't kill the tick
        temp = hottest_temp()
        self.query_one("#temp-line", Label).update(
            f"temp {temp:.0f}°C" if temp is not None else "")

        # net / disk rates
        net = psutil.net_io_counters()
        if self._prev_net is not None:
            dt = now - self._prev_net[0]
            prev = self._prev_net[1]
            self.query_one("#net-line", Label).update(
                f"net   ▼ {fmt_rate((net.bytes_recv - prev.bytes_recv) / dt)}"
                f"   ▲ {fmt_rate((net.bytes_sent - prev.bytes_sent) / dt)}")
        self._prev_net = (now, net)
        disk_use = psutil.disk_usage("/")
        disk = psutil.disk_io_counters()
        io_str = ""
        if disk is not None and self._prev_disk is not None:
            dt = now - self._prev_disk[0]
            prev = self._prev_disk[1]
            io_str = (f"   r {fmt_rate((disk.read_bytes - prev.read_bytes) / dt)}"
                      f"   w {fmt_rate((disk.write_bytes - prev.write_bytes) / dt)}")
        if disk is not None:
            self._prev_disk = (now, disk)
        self.query_one("#disk-line", Label).update(
            f"disk  / {disk_use.percent:.0f}%{io_str}")

        # top processes
        procs = []
        for proc in psutil.process_iter(["pid", "name", "cpu_percent", "memory_percent"]):
            info = proc.info
            if info["cpu_percent"] is not None:
                procs.append(info)
        procs.sort(key=lambda p: p["cpu_percent"], reverse=True)
        table = self.query_one("#procs", DataTable)
        table.clear()
        for info in procs[:TOP_PROCESSES]:
            table.add_row(str(info["pid"]), (info["name"] or "?")[:40],
                          f"{info['cpu_percent']:.1f}", f"{info['memory_percent']:.1f}")

        self.set_status(f"cpu {total:.0f}%   mem {mem.percent:.0f}%   {len(per_core)} cores")


def main() -> None:
    StatsPanelApp().run()
