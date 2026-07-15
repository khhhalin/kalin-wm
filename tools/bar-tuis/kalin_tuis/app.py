"""KalinPanelApp — the scaffold every panel TUI subclasses.

Fixed shape (same as the original display panel, proven docked):
Header(no clock) / one-line status / panel content / Footer with the
bindings. Subclasses implement compose_panel(), extend BINDINGS (Textual
merges them across the MRO), and build their CSS as SHARED_CSS + "..." —
App.CSS is a plain class attribute, not MRO-merged like a widget's
DEFAULT_CSS, so the concatenation has to be explicit.
"""
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.widgets import Footer, Header, Label

from .theme import KALIN_THEME, SHARED_CSS


class KalinPanelApp(App):
    PANEL_TITLE = "Panel"

    # No command palette in a 700x480 docked panel — it steals ^p and
    # clutters the footer with "palette".
    ENABLE_COMMAND_PALETTE = False

    CSS = SHARED_CSS

    BINDINGS = [
        Binding("q", "quit", "Quit"),
        Binding("ctrl+c", "quit", "Quit", show=False),
    ]

    def compose(self) -> ComposeResult:
        yield Header(show_clock=False)
        yield Label("", id="status-line")
        yield from self.compose_panel()
        yield Footer()

    def compose_panel(self) -> ComposeResult:
        raise NotImplementedError

    def on_mount(self) -> None:
        self.register_theme(KALIN_THEME)
        self.theme = KALIN_THEME.name
        self.title = self.PANEL_TITLE

    def set_status(self, markup: str) -> None:
        self.query_one("#status-line", Label).update(markup)

    def start_poll(self, seconds: float, callback) -> None:
        """Immediate first call + interval — panels that are purely
        event-driven (bluetooth, battery) skip this and subscribe instead."""
        callback()
        self.set_interval(seconds, callback)
