# Window placement

**SUPERSEDED.** The old choice was "into the [[column-layout]] strip, or as
an [[anchored-window]]" — both concepts are gone. Current placement logic
for a newly-mapped window (in priority order) lives in `mapnotify()` and is
described in [[spawn]] and [[connection-graph]]:

1. A persisted position from a previous run of this exact app instance — see [[persistence]].
2. To the right of the spawn-parent (whichever window was focused right
   before this one was created), spliced into an existing line if the
   parent already has an East neighbor — see [[connection-graph]].
3. Monitor center, for the very first window with no parent and no saved state.

The rest of this note describes the old design, kept for history:

- Window placement decided where a newly mapped window went: into the [[column-layout]] strip, or as an [[anchored-window]] at a fixed position.
- Placement only positioned windows that had not already been placed; it did not reposition a window the user had set. Covered by unit tests.
