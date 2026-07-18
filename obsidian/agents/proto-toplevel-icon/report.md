# Worker report: proto-toplevel-icon

Status: **for-review** · Branch: `fleet/proto-toplevel-icon`

## What changed and why

Implemented the two log-warning protocols via the wlroots 0.20 wrappers (both
exist in the installed headers — checked before writing any code, no
hand-rolled protocol code needed):

- **`xdg-toplevel-icon-v1`** — `code/src/modules/protocols/toplevel_icon.c`
  (new TU). Registers `wlr_xdg_toplevel_icon_manager_v1_create(dpy, 1)` and
  listens for `set_icon`. Per-toplevel icon state lives in a module-local
  `wl_list` keyed by `struct wlr_xdg_toplevel *` — no `Client` field added, per
  the brief. Each entry refs the icon (`wlr_xdg_toplevel_icon_v1_ref`; the
  wlroots header requires a ref to use the icon past the event) and unrefs it
  on replacement, on the protocol's explicit NULL/"remove icon" request, and on
  toplevel destroy (own `events.destroy` listener per entry). No
  `set_sizes()` call — empty list = no preference, honest until the taskbar
  consumer exists. Nothing reads the map yet; exposing icons to the Quickshell
  taskbar is the planned follow-up task.
- **`xdg-system-bell-v1`** — `code/src/modules/protocols/system_bell.c` (new
  TU, not a one-liner because the brief asked for a ring handler). Registers
  the global and logs rings at `WLR_DEBUG`. No audible/visual bell wired.

`dwl.c` got exactly the module-idiom lines: two forward decls next to
`toplevel_export_init` (code/src/dwl.c:307-310) and two init calls after
`toplevel_export_init(dpy)` in `setup()` (code/src/dwl.c:4107-4108). No other
`dwl.c` changes, keeping the gate merge against the keeper's uncommitted
`dwl.c` edits as small as possible.

## Files touched

- `code/src/modules/protocols/toplevel_icon.c` — new
- `code/src/modules/protocols/system_bell.c` — new
- `code/src/dwl.c` — 6 added lines (4 forward-decl/comment, 2 init calls)
- `Makefile` — the two new TUs appended to `SRCS`, nothing else
- `obsidian/implementation/protocols.md` — both protocols moved from
  "Missing — confirmed by our own logs" to "Already implemented" with module
  pointers; the Missing list now holds only text-input-v3/input-method-v2
- `obsidian/agents/proto-toplevel-icon/report.md` — this file

## Verification

- `nix develop -c make clean all` — exit 0; the only warning in the whole
  build is the pre-existing `-Woverlength-strings` on
  `code/config/default_binds.h` (bind_engine.c), untouched by this task. The
  two new TUs compile warning-free.
- `nix develop -c make test-unit` — 25/25 passed, 0 assertion failures;
  window-shader math tests all OK.
- VM tests deliberately not run (keeper-only). Runtime behavior (warnings
  actually gone from `/tmp/kalinwm.log`, an icon-setting client exercising
  `set_icon`) is not verified here — recommend the keeper's usual VM check.

## Notes for the keeper

- **Task note missing:** `obsidian/tasks/proto-toplevel-icon.md` does not
  exist in this worktree (only other tasks' notes do), so I could not set its
  status to for-review. Nothing else blocked; flagging instead of creating
  it since `obsidian/tasks/` is outside my scope.
- Icon state is intentionally write-only for now; the follow-up
  taskbar-exposure task will need an accessor (e.g. lookup by
  `wlr_xdg_toplevel *` or wiring into `foreign_toplevel.c`) — out of my scope.
