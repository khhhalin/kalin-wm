"""Clipboard history panel — cliphist browser replacing the old
`kalin-clip-picker-loop` fzf wrapper.

The loop wrapper existed only because fzf exits on selection and the docked
panel's process is spawned once and never respawned; this app copies on
enter and *stays running*, so no loop is needed. (The separate one-shot
`kalin-clip-picker` stays for the Super+V keybind, where exit-on-select is
the right behavior.)

fzf-style focus model: the filter Input keeps keyboard focus the whole time
(typing always filters), while up/down/enter/delete act on the list via
priority app bindings — printable keys can't be app-bound without stealing
them from the Input, which is also why quitting here is ctrl+c, not `q`.
"""
from __future__ import annotations

import subprocess

from rich.text import Text

from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import Vertical
from textual.timer import Timer
from textual.widgets import Input, OptionList, Static
from textual.widgets.option_list import Option

from .app import KalinPanelApp
from .theme import SHARED_CSS
from .util import run

POLL_INTERVAL = 5.0
MAX_ENTRIES = 500          # cliphist keeps far more; cap what we render
PREVIEW_DEBOUNCE = 0.15    # s between highlight and the decode subprocess
PREVIEW_MAX_CHARS = 2000   # matches kalin-clip-picker's head -c 2000
BINARY_MARKER = "[[ binary data"  # cliphist's own placeholder for image entries


class ClipboardPanelApp(KalinPanelApp):
    """Filterable cliphist history with live preview; copy without exiting."""

    PANEL_TITLE = "Clipboard History"

    CSS = SHARED_CSS + """
    #filter {
        border: round #4a3625;
        height: 3;
    }

    #filter:focus {
        border: round $primary;
    }

    #entries {
        height: 1fr;
        border: none;
        scrollbar-size-vertical: 1;
    }

    #preview-card {
        height: 8;
        margin: 0 1 0 1;
    }

    #preview {
        height: 100%;
        color: $text-muted;
        overflow-y: hidden;
    }
    """

    BINDINGS = [
        # priority=True: fire even while the filter Input has focus.
        Binding("up", "highlight_up", "Up", show=False, priority=True),
        Binding("down", "highlight_down", "Down", show=False, priority=True),
        Binding("enter", "copy_selected", "Copy", priority=True),
        Binding("delete", "delete_selected", "Delete entry", priority=True),
        Binding("escape", "clear_filter", "Clear filter", show=False, priority=True),
    ]

    def __init__(self) -> None:
        super().__init__()
        self._raw_lines: list[str] = []   # full cliphist lines (id\tpreview), decode/delete take them on stdin
        self._shown_lines: list[str] = []  # after filtering, parallel to the OptionList
        self._preview_timer: Timer | None = None

    def compose_panel(self) -> ComposeResult:
        yield Input(placeholder="filter…", id="filter")
        yield OptionList(id="entries")
        with Vertical(id="preview-card", classes="card"):
            # markup=False everywhere clipboard *content* lands: history text
            # is arbitrary and must never be parsed as style markup.
            yield Static("", id="preview", markup=False)

    def on_mount(self) -> None:
        super().on_mount()
        self.query_one("#filter", Input).focus()
        self.start_poll(POLL_INTERVAL, self._reload)

    # ── data ────────────────────────────────────────────────────────────────

    def _reload(self) -> None:
        try:
            out = run(["cliphist", "list"])
        except (RuntimeError, FileNotFoundError, subprocess.TimeoutExpired) as exc:
            self.set_status(f"[red]cliphist failed:[/red] {exc}")
            return
        lines = out.splitlines()[:MAX_ENTRIES]
        if lines == self._raw_lines:
            return
        self._raw_lines = lines
        self._refilter()

    def _refilter(self) -> None:
        needle = self.query_one("#filter", Input).value.lower()
        entries = self.query_one("#entries", OptionList)
        old_highlight = entries.highlighted
        self._shown_lines = [
            line for line in self._raw_lines
            if needle in line.split("\t", 1)[-1].lower()
        ]
        entries.clear_options()
        for line in self._shown_lines:
            clip_id, _, text = line.partition("\t")
            text = text.strip() or "(empty)"
            # Rich Text, not markup: same arbitrary-content concern as the
            # preview. Truncated to one row; the preview pane has the rest.
            entries.add_option(Option(Text.assemble(
                (f"{clip_id:>5}", "dim"), "  ", text[:78])))
        if self._shown_lines:
            count = len(self._shown_lines)
            entries.highlighted = min(old_highlight or 0, count - 1)
            self.set_status(f"{count} / {len(self._raw_lines)} entries")
        else:
            self.set_status(f"0 / {len(self._raw_lines)} entries")
            self.query_one("#preview", Static).update("")

    def _selected_line(self) -> str | None:
        idx = self.query_one("#entries", OptionList).highlighted
        if idx is None or not (0 <= idx < len(self._shown_lines)):
            return None
        return self._shown_lines[idx]

    # ── events ──────────────────────────────────────────────────────────────

    def on_input_changed(self, event: Input.Changed) -> None:
        self._refilter()

    def on_option_list_option_highlighted(self, event: OptionList.OptionHighlighted) -> None:
        # Debounce: a held arrow key must not spawn a decode per row skipped.
        if self._preview_timer is not None:
            self._preview_timer.stop()
        self._preview_timer = self.set_timer(PREVIEW_DEBOUNCE, self._update_preview)

    def _update_preview(self) -> None:
        line = self._selected_line()
        preview = self.query_one("#preview", Static)
        if line is None:
            preview.update("")
            return
        if BINARY_MARKER in line:
            preview.update(line.partition("\t")[2])
            return
        try:
            content = run(["cliphist", "decode"], input_text=line)
        except (RuntimeError, subprocess.TimeoutExpired) as exc:
            preview.update(f"decode failed: {exc}")
            return
        preview.update(content[:PREVIEW_MAX_CHARS])

    # ── actions ─────────────────────────────────────────────────────────────

    def action_highlight_up(self) -> None:
        entries = self.query_one("#entries", OptionList)
        if entries.option_count:
            entries.highlighted = max((entries.highlighted or 0) - 1, 0)

    def action_highlight_down(self) -> None:
        entries = self.query_one("#entries", OptionList)
        if entries.option_count:
            entries.highlighted = min((entries.highlighted or 0) + 1, entries.option_count - 1)

    def action_copy_selected(self) -> None:
        line = self._selected_line()
        if line is None:
            return
        # Bytes end to end: image entries must survive the decode → wl-copy
        # hop unmangled, so no text= here.
        try:
            decode = subprocess.run(["cliphist", "decode"], input=line.encode(),
                                    capture_output=True, timeout=5, check=True)
            subprocess.run(["wl-copy"], input=decode.stdout, timeout=5, check=True)
        except (subprocess.SubprocessError, FileNotFoundError) as exc:
            self.set_status(f"[red]copy failed:[/red] {exc}")
            return
        self.set_status("[green]copied ✓[/green]")

    def action_delete_selected(self) -> None:
        line = self._selected_line()
        if line is None:
            return
        try:
            run(["cliphist", "delete"], input_text=line)
        except (RuntimeError, subprocess.TimeoutExpired) as exc:
            self.set_status(f"[red]delete failed:[/red] {exc}")
            return
        self._raw_lines.remove(line)
        self._refilter()

    def action_clear_filter(self) -> None:
        self.query_one("#filter", Input).value = ""


def main() -> None:
    ClipboardPanelApp().run()
