"""kalin-bar-tui — dispatcher for the docked bar panel TUIs.

Panel modules are imported lazily so each panel process only pays for (and
only needs) its own backend deps — psutil for stats, dbus-fast for
bluetooth/battery — instead of every panel importing everything.
"""
import importlib
import sys

# Keep in sync with BottomBar.qml's DockedPanel commands and
# obsidian/bar-tuis.md.
PANELS = ("wifi", "bluetooth", "mixer", "stats", "clipboard", "battery", "display")


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in PANELS:
        sys.stderr.write(f"usage: kalin-bar-tui <{'|'.join(PANELS)}>\n")
        return 2
    importlib.import_module(f"kalin_tuis.{sys.argv[1]}").main()
    return 0


if __name__ == "__main__":
    sys.exit(main())
