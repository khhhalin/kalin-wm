# Window placement

**SUPERSEDED.** The old choice was "into the [[column-layout]] strip, or as
an [[anchored-window]]" — both concepts are gone. Current placement logic
for a newly-mapped window (in priority order) lives in `mapnotify()` and is
described in [[spawn]] and [[connection-graph]]:

1. A persisted position from a previous run of this exact app instance — see [[persistence]].
2. To the right of the spawn-parent (whichever window was focused right
   before this one was created), at the parent's baseline y, and **spliced
   into the [[rail]]** — the 1D scrolling order (layout Phase 2, DONE
   2026-08-13). Successors of the parent shift right to open room; closing a
   rail member shifts them back left (gap-close). See [[rail]] and
   [[layout-impl]].
3. Under the cursor when there's no spawn-parent but the cursor is on this
   monitor; otherwise monitor center, for the very first window.

The rest of this note describes the old design, kept for history:

- Window placement decided where a newly mapped window went: into the [[column-layout]] strip, or as an [[anchored-window]] at a fixed position.
- Placement only positioned windows that had not already been placed; it did not reposition a window the user had set. Covered by unit tests.
