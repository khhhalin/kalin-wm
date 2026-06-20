# kalin-wm Roadmap

> **THE SINGLE SOURCE OF TRUTH FOR WHAT NEEDS TO BE DONE.**
>
> **AGENTS:** When you complete an item, you **MUST** update this file by changing `[ ]` to `[x]` and appending your name or a commit ref in parentheses. When you add a new task, append it to the appropriate section with `[ ]`.
>
> **This file lives in the repo root so it cannot be missed.**

---

## Phase 0: Stability & correctness (BLOCKS v1.0)

These come from the [Stability Audit](docs/obsidian-vault/research/active-design/stability-audit.md). Nothing in v1.0 ships before these are fixed.

### Critical (crash guaranteed under specific conditions)

- [x] **C1** — Memory leak on duplicate client creation (`createnotify` overwrites `toplevel->base->data` without NULL check) (GitHub Copilot)
- [x] **C2** — NULL pointer dereference in popup handling (`commitpopup` missing NULL check after `wlr_xdg_popup_try_from_wlr_surface`) (GitHub Copilot)
- [x] **C3** — NULL keyboard state access (`keypress` uses `xkb_state` without NULL check) (GitHub Copilot)
- [x] **C4** — `grabc` NULL dereference in motion handler during drag (client destroyed while being dragged) (GitHub Copilot)
- [x] **C5** — Division by zero in crop calculation (`cropend` if window has zero size) (GitHub Copilot)
- [x] **C6** — NULL monitor access in `cropbegin` (`selmon` used without check) (GitHub Copilot)
- [x] **C7** — Division by zero in tile layout (divisor `MIN(n, m->nmaster) - i` can be zero) (GitHub Copilot)
- [x] **C8** — Division by zero in stack area of tile layout (`n - i` can be zero) (GitHub Copilot)

### High (likely crash or UB)

- [x] **H1** — Scene node creation failure not checked in `mapnotify` (GitHub Copilot)
- [x] **H2** — Unmanaged client race in `unmapnotify` (`selmon` could be NULL) (GitHub Copilot)
- [x] **H3** — Button press cursor mode change happens even if input is locked (GitHub Copilot)
- [x] **H4** — Active constraint NULL access in `cursorwarptohint` (GitHub Copilot)
- [x] **H5** — Double-free in `cropcancel` (modular version does not NULL pointers after destroy) (GitHub Copilot)
- [x] **H6** — Use-after-free in `cropdraw` (border arrays accessed without init check) (GitHub Copilot)
- [x] **H7** — Infinite layout recursion in `infinite()` (`arrange` called recursively without termination guarantee) (GitHub Copilot)
- [x] **H8** — Viewport division by zero (`viewport.zoom` theoretically 0) (GitHub Copilot)
- [x] **H9** — NULL return from `focustop()` not checked by many callers (GitHub Copilot)
- [x] **H10** — `xytomon()` returns NULL when cursor is outside all outputs (GitHub Copilot)

### Medium & Low (tech debt)

- [x] **M1** — Key repeat race condition (`group->keysyms` may be freed during repeat) (GitHub Copilot)
- [x] **M2** — Uninitialized world coordinates in `infinite()` (GitHub Copilot)
- [x] **M3** — Scene buffer scale with zero size (GitHub Copilot)
- [x] **M4** — Layer surface without monitor leaks resources (GitHub Copilot)
- [x] **M5** — `VISIBLEON` macro logical errors (GitHub Copilot)
- [x] **M6** — `wl_list_remove` on uninitialized list for unmanaged clients (GitHub Copilot)
- [x] **M7** — `LISTEN_STATIC` macro leaks listener allocations (GitHub Copilot)
- [x] **M8** — Race condition in `client_set_buffer_scale` (GitHub Copilot)
- [x] **L1** — Unchecked `ecalloc` returns throughout codebase (GitHub Copilot)
- [x] **L2** — Buffer overflow in `strncpy` (may not null-terminate) (GitHub Copilot)

> **Re-audit 2026-06-20 (Claude):** All Phase 0 items above were re-verified against the
> *live* code — i.e. dwl.c plus the `.c` modules it `#include`s (`modules/crop/crop_mode.c`,
> `modules/layout/layout_world.c`, etc.), not the stale duplicate copies under `code/src/`
> and `code/src/modules/`. The audited crash/UB guards (C2/C3/C5/H1/H7/H8/H9/M1, …) are
> genuinely present. Several items (e.g. the upstream-dwl `tile()`/`monocle()` div-by-zero)
> are **N/A** here because this fork has only the `infinite()` layout. Separately, the build
> itself was broken (duplicate `same_column_x`) and a real buffer-scaling bug was found and
> fixed — see CHANGELOG.

---

## Phase 1: v1.0 — Feature-complete infinite canvas compositor

### P0 (must have)

- [x] Infinite canvas with world coordinates
- [x] Viewport pan / zoom / reset
- [x] Buffer scaling
- [x] Basic window management
- [x] Multi-monitor support
- [x] Crop mode
- [x] 2D window placement (column + anchored)
- [x] Directional focus navigation (cone search)
- [x] Camera pan with `Super+Shift+Arrows`
- [ ] Overview mode (decision deferred — see `active-design/overview-mode.md`)
- [x] Keyboard movement for anchored windows (GitHub Copilot)
- [x] Visual focus indicator (glow / thicker border / focus ring) (GitHub Copilot)

### P1 (important)

- [ ] Smooth animations (spring physics for pan/zoom, window open/close)
- [ ] Touchpad gestures (pinch-to-zoom, two-finger pan)
- [ ] Window shadows
- [x] Persistent world state (save / restore window positions across sessions) (GitHub Copilot)
- [x] Off-screen window indicators (edge arrows pointing to windows outside viewport) (GitHub Copilot)
- [x] Improved `printstatus()` output (viewport x/y/zoom, follow mode, window positions) (GitHub Copilot)
- [x] Spawn failure feedback (pipe-based error reporting or at minimum logging) (GitHub Copilot)
- [x] Exit confirmation (double-press or visual feedback) (GitHub Copilot)

### P2 (nice to have for v1.0)

- [ ] Rounded corners
- [ ] Minimap (corner overview of all windows + viewport rectangle)
- [ ] Bookmarks (save named viewport positions and jump to them)
- [ ] Magnetic snapping (windows snap to each other / to grid)
- [ ] Anchor mode visual distinction (different border color for anchored vs column)
- [ ] Crop mode on-screen banner indicator
- [ ] Cursor state feedback during pan / move / resize

---

## Phase 2: Post-v1.0

- [ ] Startup screen / loading indicator with keybinding hints
- [ ] Built-in window title bars (or hover overlay labels)
- [ ] OSD overlay for viewport state (fades in on change, out after delay)
- [ ] Structured logging with categories (input, viewport, layout, client)
- [ ] Debug mode with verbose diagnostics
- [ ] Core dump / crash recovery guidance
- [ ] IPC protocol (replace stdout status stream with proper IPC)

---

## How to use this roadmap

1. **Pick the highest-priority unchecked item** you are qualified to fix.
2. **Before starting:** read the linked design doc or audit entry if one exists.
3. **After merging:** come back here and mark it `[x]`.
4. **If a task is too vague:** refine it in-place or open a discussion.
5. **If you discover a new requirement:** add it to the appropriate section with `[ ]`.

---

*Last updated: 2026-04-20*
