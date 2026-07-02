# Fixes Summary

> Summary of all stability, memory, and UX fixes applied to kalin-wm.

## Date
2026-04-10

## Issues Fixed

### 2026-04-20 Updates

| Issue | File | Fix |
|-------|------|-----|
| C1: Memory leak on duplicate client creation | dwl.c, client.c | Added guard in `createnotify()` to return early when `toplevel->base->data` is already set |
| C2: NULL popup dereference in commit path | dwl.c, client.c | Added `if (!popup) return;` before using popup fields in `commitpopup()` |
| C3: NULL keyboard state access | dwl.c, input.c | Added early return when `xkb_state` is NULL in `keypress()` |
| C4: `grabc` NULL dereference during drag | dwl.c, input.c | Added guard to reset `cursor_mode` to normal and return when drag state exists without `grabc` |
| C5: Division by zero in crop calculation | crop.c, modules/crop_mode.c | Added guard to cancel crop when target window width/height is non-positive |
| C6: NULL monitor access in cropbegin | crop.c, modules/crop_mode.c | Added early return when `selmon` is NULL before crop setup |
| C7: Division by zero in tile master area | client.c, layouts/tile.c | Clamped master divisor to minimum 1 before height division |
| C8: Division by zero in tile stack area | client.c, layouts/tile.c | Clamped stack divisor to minimum 1 before height division |
| H1: Scene node creation failure unchecked | dwl.c, client.c | Added NULL check after `wlr_scene_tree_create()` with safe early return |
| H2: Unmanaged unmap race with NULL monitor | dwl.c, client.c | Added `selmon` guard before focus recovery in unmanaged `unmapnotify()` path |
| H3: Cursor mode changed while input locked | dwl.c, input.c | Moved `cursor_mode = CurPressed` to occur only after lock-state check in `buttonpress()` |
| H4: Active constraint NULL access | dwl.c, input.c | Added early return in `cursorwarptohint()` when `active_constraint` is NULL |
| H5: Double-free risk in crop cancel | modules/crop_mode.c, modules/crop_viewport_layout.c, crop.c | Added NULL checks before scene-node destroy and always cleared crop editor pointers after destroy |
| H6: Crop draw use-after-free risk | modules/crop_mode.c, modules/crop_viewport_layout.c, crop.c | Added pointer validation guards in `cropdraw()` before border/handle/crosshair or selection node access |
| H7: Infinite layout recursion risk | modules/layout_world.c, layouts/infinite.c | Added explicit re-entry guard in `infinite()` to prevent recursive layout re-entry |
| H8: Viewport division by zero | modules/viewport_ops.c, viewport.c | Used guarded zoom divisor (`zoom > 0 ? zoom : 1`) in viewport centering/panning paths |
| H9: Unchecked NULL from `focustop()` callers | dwl.c, client.c | Added explicit `focustop(NULL)` guard and centralized NULL-safe `focus_top()` helper used by focus handoff call sites |
| M1: Key repeat race on borrowed keysyms | dwl.c, input.c, kalin.h | Switched repeat path to owned `keysyms` copies, freeing/replacing on update and on keyboard-group destroy |
| M2: Uninitialized world/layout geometry path | modules/layout_world.c, layouts/infinite.c | Added early return when monitor work area dimensions are non-positive before placing/arranging world windows |
| M3: Scene buffer scaling with zero destination size | dwl.c | Clamped computed buffer destination size to minimum 1x1 before applying scene buffer scaling |
| M4: Layer surface without monitor leaks resources | client.c, dwl.c | Ensured layer-surface allocation only occurs after output/monitor validation, preventing leak on early return |
| M5: `VISIBLEON` macro logical errors | kalin.h, layout.h, dwl.c, input.c, layouts/infinite.c | Centralized the macro definition and added NULL client guard; removed divergent local redefinitions |
| M6: `wl_list_remove` on uninitialized list for unmanaged clients | client.c, dwl.c | Guarded list removals with link validity checks to avoid removing uninitialized or already-removed links |
| M7: `LISTEN_STATIC` macro leaks listener allocations | kalin.h, dwl.c, client.c | Added tracked static listener allocation with cleanup on shutdown and handler destruction |
| M8: Race condition in `client_set_buffer_scale` | dwl.c | Added scale validation and safe iteration over scene-surface children with parent checks |
| L2: Buffer overflow in `strncpy` (may not null-terminate) | client.c, dwl.c | Replaced `strncpy` with `snprintf` for layout symbols to guarantee null termination |
| L1: Unchecked `ecalloc` returns throughout codebase | util.h | Annotated `ecalloc` as returns-non-null and `die` as noreturn to document fatal allocation behavior |
| H10: `xytomon()` NULL monitor fallback | dwl.c, client.c | Added fallback to selected/first monitor when pointer is outside all outputs |

### Critical Stability (4 issues)

| Issue | File | Fix |
|-------|------|-----|
| Division by zero in cropend | dwl.c | Added check before division |
| NULL monitor in cropbegin | dwl.c | Added guard clause |
| NULL keyboard state | dwl.c | Added NULL check |
| grabc NULL dereference | dwl.c | Added NULL check |

### Memory Management (3 issues)

| Issue | File | Fix |
|-------|------|-----|
| Double-free in cropcancel | dwl.c | Set NULL after destroy |
| NULL pointer in applyrules | dwl.c | Null-coalesce to "" |
| Missing wl_list_init | dwl.c | Explicit init after ecalloc |

### UX Polish (5 improvements)

| Feature | Implementation |
|---------|----------------|
| Viewport status output | Enhanced printstatus() |
| Exit confirmation | 2-second double-press |
| Better focus indication | Border width 1px -> 3px |
| Crop mode feedback | White flash + status |
| Error logging | wlr_log() calls |

## Build Status

- Build: Yes Successful
- Binary: 293KB (+5KB for fixes)
- Warnings: No new warnings

## Full Reports

- [[stability-audit]] - 23 issues analyzed
- [[memory-audit]] - Memory safety review  
- [[ux-polish]] - UX recommendations
