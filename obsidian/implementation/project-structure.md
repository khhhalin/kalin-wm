# Project structure

- The kalin-wm repository is organized by role:

- `code/` — the code tree: `code/config/` ([[compile-time-config]]),
  `code/include/` (headers; `kalin.h` is the umbrella), `code/include/protocols/`
  (generated protocol headers), `code/src/` ([[dwl-fork|dwl.c]] + modules),
  `code/src/modules/` (runtime modules: crop, layout, viewport, ui, input,
  foreign_toplevel, ipc).
- `scripts/` — runnable helpers (`run-tty`, `run-nested`, `dev/` diagnostics).
- `tests/` — C unit/integration tests and scripts; `code/tests/` holds the unit
  test source.
- `protocols/` — vendored Wayland protocol XML.
- `docs/` — man page, desktop entry, and changelog (changelog content also folded
  into the [[ledger]]).
- `obsidian/` — this vault: the text model of the project, with the deep
  [[research/README|research subtree]].
- `backups/` — local snapshots, not active code.

- Root holds build entrypoints (`Makefile`, `flake.nix`), `AGENTS.md`, `README.md`, licenses, and changelogs.
- This note supersedes the old `docs/PROJECT_STRUCTURE.md`.
