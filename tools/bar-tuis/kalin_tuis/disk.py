"""Disk usage panel — ncdu-style du browser for the docked bar.

Read-only by design: freeing space means judgment calls (GC roots, nix
generations, "is this zip a duplicate") that belong in a real shell, not
one keypress inside a hover panel. This answers "what is taking up space"
and stays out of the deleting business.

Sizes come from one `du -xa --max-depth=1` per visited directory, run
async so the UI never blocks; results are cached for the panel's lifetime
(the process lives as long as the dock), so revisiting a directory is
instant and `r` is the explicit "reality changed" escape hatch. `-x`
stays on one filesystem — a machine-space question, not a mounts tour.
"""
from __future__ import annotations

import asyncio
import os
import shutil

from rich.text import Text

from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import Vertical
from textual.widgets import Label, OptionList
from textual.widgets.option_list import Option

from .app import KalinPanelApp
from .theme import PRIMARY_HEX, SHARED_CSS, TEXT_MUTED_HEX
from .widgets import Gauge

DU_TIMEOUT = 300.0     # cold-cache du over /nix/store takes minutes, not seconds
BAR_CELLS = 12
BAR_BLOCKS = " ▏▎▍▌▋▊▉█"
NAME_MAX = 46          # panel is ~87 cells; size+bar+pct columns take the rest


async def du_children(path: str) -> tuple[int, list[tuple[int, str, bool]]]:
    """(total_bytes, [(bytes, name, is_dir)] sorted largest-first).

    du exits 1 when some subtrees were unreadable (normal as a plain user
    under /) while still printing every subtree it could read — accept it
    and show the partial truth rather than nothing.
    """
    proc = await asyncio.create_subprocess_exec(
        "du", "-xa", "--max-depth=1", "-B1", "--", path,
        stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL)
    try:
        stdout, _ = await asyncio.wait_for(proc.communicate(), DU_TIMEOUT)
    except asyncio.TimeoutError:
        proc.kill()
        raise RuntimeError(f"du timed out after {DU_TIMEOUT:.0f}s") from None
    if proc.returncode not in (0, 1):
        raise RuntimeError(f"du exited {proc.returncode}")
    total = 0
    children: list[tuple[int, str, bool]] = []
    for line in stdout.decode(errors="replace").splitlines():
        size_str, _, entry = line.partition("\t")
        if not entry:
            continue
        size = int(size_str)
        if entry == path:
            total = size
            continue
        name = os.path.basename(entry)
        children.append((size, name, os.path.isdir(os.path.join(path, name))
                         and not os.path.islink(os.path.join(path, name))))
    children.sort(key=lambda c: c[0], reverse=True)
    return total, children


def fmt_size(size: int) -> str:
    value = float(size)
    for unit in ("B", "K", "M", "G", "T"):
        if value < 1024 or unit == "T":
            return f"{value:.0f}{unit}" if unit == "B" else f"{value:.1f}{unit}"
        value /= 1024
    return f"{value:.1f}T"


def size_bar(size: int, largest: int) -> str:
    if largest <= 0:
        return " " * BAR_CELLS
    eighths = round(size / largest * BAR_CELLS * 8)
    full, rem = divmod(eighths, 8)
    bar = "█" * full + (BAR_BLOCKS[rem] if rem else "")
    return bar.ljust(BAR_CELLS)


class DiskPanelApp(KalinPanelApp):
    """Browse where the space went: du per directory, largest first."""

    PANEL_TITLE = "Disk Usage"

    CSS = SHARED_CSS + """
    #fs-card {
        margin: 0 1;
    }

    #cwd-line {
        padding: 0 1;
        height: 1;
        color: $text-muted;
    }

    #entries {
        height: 1fr;
        border: none;
        margin: 0 1;
        scrollbar-size-vertical: 1;
    }
    """

    BINDINGS = [
        # enter is display-only: a focused OptionList consumes it, the live
        # path is on_option_list_option_selected (see bar-tuis.md gotcha).
        Binding("enter", "noop", "Open"),
        Binding("backspace", "go_up", "Up"),
        Binding("left", "go_up", "Up", show=False),
        Binding("r", "rescan", "Rescan"),
    ]

    def __init__(self) -> None:
        super().__init__()
        self._cwd = "/"
        self._cache: dict[str, tuple[int, list[tuple[int, str, bool]]]] = {}
        self._shown: list[tuple[int, str, bool]] = []  # parallel to the OptionList

    def compose_panel(self) -> ComposeResult:
        with Vertical(id="fs-card", classes="card"):
            yield Gauge("Filesystem", id="fs-gauge")
        yield Label("", id="cwd-line")
        yield OptionList(id="entries")

    def on_mount(self) -> None:
        super().on_mount()
        self.query_one("#fs-card").border_title = "filesystem"
        self._refresh_fs_gauge()
        self._enter_dir(self._cwd)

    # ── data ────────────────────────────────────────────────────────────────

    def _refresh_fs_gauge(self) -> None:
        usage = shutil.disk_usage(self._cwd)
        self.query_one("#fs-gauge", Gauge).update_value(
            round(usage.used / usage.total * 100))
        self.query_one("#fs-card").border_title = (
            f"filesystem · {fmt_size(usage.used)} used"
            f" · {fmt_size(usage.free)} free")

    def _enter_dir(self, path: str) -> None:
        self._cwd = path
        self.query_one("#cwd-line", Label).update(path)
        cached = self._cache.get(path)
        if cached is not None:
            self._render(*cached)
            return
        self.query_one("#entries", OptionList).clear_options()
        self._shown = []
        self.set_status(f"scanning {path} …")
        self.run_worker(self._scan(path), exclusive=True)

    async def _scan(self, path: str) -> None:
        try:
            total, children = await du_children(path)
        except (RuntimeError, OSError, ValueError) as exc:
            self.set_status(f"[red]du failed:[/red] {exc}")
            return
        self._cache[path] = (total, children)
        if path == self._cwd:
            self._render(total, children)

    def _render(self, total: int, children: list[tuple[int, str, bool]]) -> None:
        entries = self.query_one("#entries", OptionList)
        entries.clear_options()
        self._shown = children
        largest = children[0][0] if children else 0
        for size, name, is_dir in children:
            pct = size / total * 100 if total else 0.0
            shown_name = name if len(name) <= NAME_MAX else name[:NAME_MAX - 1] + "…"
            entries.add_option(Option(Text.assemble(
                (f"{fmt_size(size):>8}", TEXT_MUTED_HEX), "  ",
                (size_bar(size, largest), PRIMARY_HEX), " ",
                (f"{pct:4.0f}%", TEXT_MUTED_HEX), "  ",
                (shown_name + "/", "bold") if is_dir else (shown_name, ""))))
        if children:
            entries.highlighted = 0
            entries.focus()
        self.set_status(f"{len(children)} entries · {fmt_size(total)} total")
        self._refresh_fs_gauge()

    # ── events / actions ────────────────────────────────────────────────────

    def on_option_list_option_selected(self, event: OptionList.OptionSelected) -> None:
        if not (0 <= event.option_index < len(self._shown)):
            return
        size, name, is_dir = self._shown[event.option_index]
        if is_dir:
            self._enter_dir(os.path.join(self._cwd, name))

    def action_go_up(self) -> None:
        parent = os.path.dirname(self._cwd.rstrip("/")) or "/"
        if parent != self._cwd:
            self._enter_dir(parent)

    def action_rescan(self) -> None:
        self._cache.pop(self._cwd, None)
        self._enter_dir(self._cwd)

    def action_noop(self) -> None:
        pass


def main() -> None:
    DiskPanelApp().run()
