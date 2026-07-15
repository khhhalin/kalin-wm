"""Shared kalin look for all bar TUIs — the single source of truth.

Every hex below is copied from one of two upstream sources, so a re-rice
there must be mirrored here (and vice versa):
  - foot.ini (~/environment/dotfiles/foot/foot.ini) — the terminal the
    panels render in.
  - Theme.qml (~/environment/quickshell/modules/services/Theme.qml) — the
    bar chrome the panels dock against.

BACKGROUND must stay *exactly* foot's background (#1e1915): foot.ini sets
alpha-mode=matching, which makes only cells whose background equals that
value translucent — one digit off and the panels turn into opaque slabs
instead of blending with the bar like every other terminal.

Hardcoded hex rather than ansi_color=True passthrough: ANSI mode caps
Textual at 16 colors (no hover/selection shades, no muted-text tiers) and
foot's 16 slots don't contain the bar's signature amber #f0a030 at all.
"""
from textual.theme import Theme

KALIN_THEME = Theme(
    name="kalin",
    dark=True,
    background="#1e1915",   # foot background — exact match required, see above
    foreground="#e7d8bf",   # foot foreground
    surface="#332419",      # Theme.qml surface
    panel="#2b1f15",        # Theme.qml surfaceAlt
    primary="#f0a030",      # Theme.qml accent (amber) — selection/borders
    secondary="#c39a56",    # foot regular3 (sand)
    accent="#e8833a",       # Theme.qml accentBlue (burnt orange)
    error="#e0552f",        # Theme.qml error
    warning="#ffb347",      # Theme.qml warning
    success="#9aa83f",      # Theme.qml success
    variables={
        # One tier brighter than the bar's textMuted/textSecondary pair:
        # Theme.qml uses #6b5642 for truly-vestigial text, but Textual leans
        # on $text-muted for meta lines that still need to be read, so that
        # maps to textSecondary and #6b5642 becomes $text-disabled.
        "text-muted": "#b08d5f",     # Theme.qml textSecondary
        "text-disabled": "#6b5642",  # Theme.qml textMuted
    },
)

# For Rich Text styling (Text.assemble/Style can only take concrete colors,
# not Textual theme variables) — same sources as above.
BORDER_HEX = "#4a3625"       # Theme.qml border; also literal in SHARED_CSS below
TEXT_MUTED_HEX = "#b08d5f"   # = variables["text-muted"]

# Literal #4a3625 (Theme.qml `border`, = BORDER_HEX) below instead of a custom
# theme variable — one less floating-Textual-API surface to depend on.
SHARED_CSS = """
Screen {
    background: $background;
}

/* The Header's clickable ○ icon only exists to open the command palette,
   which the panels disable. */
HeaderIcon {
    display: none;
}

/* Subtle list-cursor: default Textual paints a solid $primary block that
   drowns any per-row styling (volume bars, badges). #3d2c1c = Theme.qml
   surfaceActive. */
OptionList > .option-list--option-highlighted {
    background: #3d2c1c;
}

#status-line {
    padding: 0 1;
    color: $text-muted;
    height: 1;
}

.card {
    border: round #4a3625;
    padding: 0 1;
    height: auto;
}

.card.-selected, .card:focus-within {
    border: round $primary;
}

.dim {
    color: $text-disabled;
}
"""
