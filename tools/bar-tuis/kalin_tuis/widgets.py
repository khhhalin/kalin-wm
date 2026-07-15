"""Widgets shared by more than one panel."""
from textual.app import ComposeResult
from textual.containers import Horizontal
from textual.widgets import Label, ProgressBar


class Gauge(Horizontal):
    """`label ▐███▌ NN%` — the standard row for any 0-100 value
    (brightness, volume, charge). ProgressBar's own percentage readout is
    disabled in favor of the fixed-width right-aligned label so every
    gauge's bar starts and ends in the same column."""

    DEFAULT_CSS = """
    Gauge {
        height: 1;
    }
    Gauge > .gauge-label {
        width: 12;
    }
    Gauge > ProgressBar {
        width: 1fr;
        margin: 0 1;
    }
    Gauge > .gauge-pct {
        width: 5;
        text-align: right;
    }
    """

    def __init__(self, label: str, percent: int | None = None, **kwargs) -> None:
        super().__init__(**kwargs)
        self._label = label
        self._percent = percent

    def compose(self) -> ComposeResult:
        yield Label(self._label, classes="gauge-label")
        yield ProgressBar(total=100, show_eta=False, show_percentage=False)
        yield Label("", classes="gauge-pct")

    def on_mount(self) -> None:
        if self._percent is not None:
            self.update_value(self._percent)

    def update_value(self, percent: int) -> None:
        self._percent = percent
        self.query_one(ProgressBar).update(progress=percent)
        self.query_one(".gauge-pct", Label).update(f"{percent}%")
