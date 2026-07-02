# Persistence

Persistence saves and restores window world positions across sessions. It lets
the [[infinite-canvas]] survive a restart so windows reappear where they were.

State is stored as JSON and implemented in `code/src/persistence.c` (its own
translation unit). It records the [[world-coordinates]] of windows, including
each [[anchored-window]].
