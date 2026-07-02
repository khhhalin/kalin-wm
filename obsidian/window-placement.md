# Window placement

Window placement decides where a newly mapped window goes on the
[[infinite-canvas]]: into the [[column-layout]] strip, or as an
[[anchored-window]] at a fixed position.

Placement only positions windows that have not already been placed; it does not
reposition a window the user has set. This invariant is covered by unit tests.

The placement design is documented as an architecture decision in
[[research/architecture/decisions/window-placement|the placement decision]] and
in [[research/active-design/window-placement|the active-design note]].
