"""Audio mixer panel — pw-dump/wpctl based wiremix replacement.

`pw-dump` provides the node list (+ the `default` metadata object); the
per-node volume/mute state comes from `wpctl get-volume <id>` instead of
the node's own Props param — for ALSA devices WirePlumber keeps the real
volume/mute on the *device route*, and the node Props can read 1.0/false
while wpctl (the ground truth the bar widget also polls) says otherwise.
wpctl's value is already cubic-scaled, so displayed % = value * 100, and
writes go through `wpctl set-volume <id> N%` symmetrically.
"""
from __future__ import annotations

import json
import re
import subprocess

from rich.text import Text

from textual.app import ComposeResult
from textual.binding import Binding
from textual.widgets import OptionList
from textual.widgets.option_list import Option

from .app import KalinPanelApp
from .theme import BORDER_HEX, KALIN_THEME, SHARED_CSS, TEXT_MUTED_HEX
from .util import run

POLL_INTERVAL = 2.0
VOLUME_STEP = 5
VOLUME_MAX = 150   # wpctl's own soft ceiling; the default sink already sits >100 here
BAR_WIDTH = 20
NAME_WIDTH = 34
VOLUME_RE = re.compile(r"Volume:\s*([0-9.]+)")

SECTIONS = (
    ("sinks", ("Audio/Sink",)),
    ("sources", ("Audio/Source",)),
    ("streams", ("Stream/Output/Audio", "Stream/Input/Audio")),
)


class AudioNode:
    def __init__(self, obj: dict) -> None:
        info = obj.get("info") or {}
        props = info.get("props") or {}
        self.id: int = obj["id"]
        self.media_class: str = props.get("media.class", "")
        self.node_name: str = props.get("node.name", "")
        if self.media_class.startswith("Stream/"):
            app = props.get("application.name") or props.get("node.name") or "?"
            media = props.get("media.name", "")
            self.label = f"{app}: {media}" if media and media != app else app
            self.label += "  ⬤" if self.media_class == "Stream/Input/Audio" else ""
        else:
            self.label = (props.get("node.description")
                          or props.get("node.nick") or self.node_name or "?")
        self.volume_pct: int | None = None
        self.muted = False

    def read_volume(self) -> None:
        """Fill volume/mute from wpctl (see module docstring for why not
        the node's Props). Nodes without a volume control stay at None."""
        try:
            out = run(["wpctl", "get-volume", str(self.id)])
        except (RuntimeError, FileNotFoundError, subprocess.TimeoutExpired):
            return
        match = VOLUME_RE.search(out)
        if match:
            self.volume_pct = round(float(match.group(1)) * 100)
            self.muted = "MUTED" in out

    def snapshot(self) -> tuple:
        return (self.id, self.label, self.volume_pct, self.muted)


def query_graph() -> tuple[dict[str, list[AudioNode]], dict[str, str]]:
    """-> ({section: nodes}, {"sink": default_node_name, "source": ...})"""
    objects = json.loads(run(["pw-dump"], timeout=10))
    sections: dict[str, list[AudioNode]] = {name: [] for name, _ in SECTIONS}
    defaults: dict[str, str] = {}
    for obj in objects:
        if obj.get("type") == "PipeWire:Interface:Node":
            props = (obj.get("info") or {}).get("props") or {}
            media_class = props.get("media.class", "")
            for name, classes in SECTIONS:
                if media_class in classes:
                    sections[name].append(AudioNode(obj))
                    break
        elif (obj.get("type") == "PipeWire:Interface:Metadata"
              and (obj.get("props") or {}).get("metadata.name") == "default"):
            for entry in obj.get("metadata", []):
                key = entry.get("key", "")
                if key in ("default.audio.sink", "default.audio.source"):
                    defaults[key.rsplit(".", 1)[1]] = (entry.get("value") or {}).get("name", "")
    for nodes in sections.values():
        nodes.sort(key=lambda n: n.label.lower())
        for node in nodes:
            node.read_volume()
    return sections, defaults


def volume_bar(node: AudioNode, is_default: bool) -> Text:
    """`* Built-in Speakers   ━━━━━━╸─────  62%` as one styled line."""
    pct = node.volume_pct if node.volume_pct is not None else 0
    filled = min(BAR_WIDTH, round(pct / 100 * BAR_WIDTH))
    label = node.label[:NAME_WIDTH]
    bar_style = TEXT_MUTED_HEX if node.muted else KALIN_THEME.primary
    pct_text = "mute" if node.muted else f"{pct:3d}%"
    return Text.assemble(
        (("* " if is_default else "  "), KALIN_THEME.primary),
        (f"{label:<{NAME_WIDTH}}", ""),
        ("━" * filled, bar_style),
        ("─" * (BAR_WIDTH - filled), BORDER_HEX),
        (f" {pct_text:>5}", TEXT_MUTED_HEX if node.muted else ""),
    )


class MixerPanelApp(KalinPanelApp):
    """Sinks / sources / streams with per-node volume + mute + default."""

    PANEL_TITLE = "Audio Mixer"

    CSS = SHARED_CSS + """
    #nodes {
        height: 1fr;
        border: none;
        padding: 0 1;
        scrollbar-size-vertical: 1;
    }
    """

    BINDINGS = [
        Binding("left,minus", "volume_down", "Vol -5"),
        Binding("right,plus", "volume_up", "Vol +5"),
        Binding("m", "toggle_mute", "Mute"),
        # footer display only — the live path is on_option_list_option_selected,
        # since the focused OptionList consumes enter before app bindings.
        Binding("enter", "set_default", "Set default"),
        # priority: Textual's own tab=focus_next is a priority binding and
        # would win otherwise; safe to take over since the OptionList is
        # this panel's only focusable widget.
        Binding("tab", "next_section", "Section", show=False, priority=True),
        Binding("r", "refresh_now", "Refresh"),
    ]

    def __init__(self) -> None:
        super().__init__()
        self._rows: list[AudioNode | None] = []  # parallel to the OptionList; None = header
        self._section_starts: list[int] = []
        self._last_snapshot: tuple | None = None

    def compose_panel(self) -> ComposeResult:
        yield OptionList(id="nodes")

    def on_mount(self) -> None:
        super().on_mount()
        self.query_one("#nodes", OptionList).focus()
        self.start_poll(POLL_INTERVAL, self._refresh)

    def _selected_node(self) -> AudioNode | None:
        idx = self.query_one("#nodes", OptionList).highlighted
        if idx is None or not (0 <= idx < len(self._rows)):
            return None
        return self._rows[idx]

    def _refresh(self) -> None:
        try:
            sections, defaults = query_graph()
        except (RuntimeError, FileNotFoundError, subprocess.TimeoutExpired,
                json.JSONDecodeError) as exc:
            self.set_status(f"[red]pw-dump failed:[/red] {exc}")
            return

        snapshot = (tuple(defaults.items()), tuple(
            node.snapshot() for name, _ in SECTIONS for node in sections[name]))
        if snapshot == self._last_snapshot:
            return
        self._last_snapshot = snapshot

        nodes_list = self.query_one("#nodes", OptionList)
        old_highlight = nodes_list.highlighted
        nodes_list.clear_options()
        self._rows = []
        self._section_starts = []
        for name, _ in SECTIONS:
            self._section_starts.append(len(self._rows))
            nodes_list.add_option(Option(
                Text(f"── {name} " + "─" * (60 - len(name)), style=TEXT_MUTED_HEX),
                disabled=True))
            self._rows.append(None)
            for node in sections[name]:
                is_default = (not node.media_class.startswith("Stream/")
                              and node.node_name != ""
                              and defaults.get(name.rstrip("s")) == node.node_name)
                nodes_list.add_option(Option(volume_bar(node, is_default)))
                self._rows.append(node)

        if self._rows:
            first_real = 1 if len(self._rows) > 1 else 0
            nodes_list.highlighted = (min(old_highlight, len(self._rows) - 1)
                                      if old_highlight is not None else first_real)

        # Show the human label for the defaults, not the raw ALSA node name.
        labels = {node.node_name: node.label
                  for nodes in sections.values() for node in nodes if node.node_name}
        sink = labels.get(defaults.get("sink", ""), defaults.get("sink", "?"))
        source = labels.get(defaults.get("source", ""), defaults.get("source", "?"))
        self.set_status(f"sink: {sink}   ·   source: {source}")

    # ── actions ─────────────────────────────────────────────────────────────

    def on_option_list_option_selected(self, event: OptionList.OptionSelected) -> None:
        self.action_set_default()

    def _wpctl(self, *args: str) -> bool:
        try:
            run(["wpctl", *args])
        except (RuntimeError, FileNotFoundError, subprocess.TimeoutExpired) as exc:
            self.set_status(f"[red]wpctl failed:[/red] {exc}")
            return False
        return True

    def _adjust_volume(self, delta: int) -> None:
        node = self._selected_node()
        if node is None or node.volume_pct is None:
            return
        new_pct = max(0, min(VOLUME_MAX, node.volume_pct + delta))
        if self._wpctl("set-volume", str(node.id), f"{new_pct}%"):
            self._refresh()

    def action_volume_down(self) -> None:
        self._adjust_volume(-VOLUME_STEP)

    def action_volume_up(self) -> None:
        self._adjust_volume(+VOLUME_STEP)

    def action_toggle_mute(self) -> None:
        node = self._selected_node()
        if node is not None and self._wpctl("set-mute", str(node.id), "toggle"):
            self._refresh()

    def action_set_default(self) -> None:
        node = self._selected_node()
        if node is None or node.media_class.startswith("Stream/"):
            return  # streams follow their sink; only devices can be default
        if self._wpctl("set-default", str(node.id)):
            self._refresh()

    def action_next_section(self) -> None:
        nodes_list = self.query_one("#nodes", OptionList)
        current = nodes_list.highlighted or 0
        for start in self._section_starts:
            # +1: land on the section's first node, not its disabled header
            if start + 1 > current and start + 1 < len(self._rows):
                nodes_list.highlighted = start + 1
                return
        if self._section_starts and len(self._rows) > 1:
            nodes_list.highlighted = self._section_starts[0] + 1

    def action_refresh_now(self) -> None:
        self._last_snapshot = None
        self._refresh()


def main() -> None:
    MixerPanelApp().run()
