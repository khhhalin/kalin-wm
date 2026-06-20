# Project Structure (kalin-wm)

This repository is organized by role to reduce root-level clutter.

## Top-level directories

- `code/` — core code tree
  - `code/config/` — compositor configuration files
  - `code/include/` — modular headers
  - `code/include/protocols/` — generated Wayland protocol headers
  - `code/src/` — compositor sources
  - `code/src/modules/` — runtime modules (crop → layout → wallpaper → viewport → commit-size → resize actions)
- `scripts/` — runnable helpers
  - `scripts/run-tty` — run compositor on TTY
  - `scripts/run-nested` — nested launcher entrypoint
  - `scripts/dev/` — diagnostics/stress/dev scripts
- `docs/` — project docs and testing guides
  - `docs/MANUAL_TESTING.md`
  - `docs/incidents/` — issue deep-dives and postmortems
  - `docs/man/` — man pages
  - `docs/desktop/` — desktop entry files
  - `docs/changelog/` — changelog history
- `patches/` — ad-hoc or historical patches
  - `patches/experiments/`
- `backups/` — local backup snapshots (not active code)
- `tests/` — C unit/integration tests and test scripts
- `docs/obsidian-vault/research/` — architecture and protocol research vault

## Root policy

Keep root focused on:

- build entrypoints (`Makefile`, `flake.nix`)
- core binary sources (`dwl.c`, `util.c`, headers)
- licenses and high-level READMEs

Configuration and protocol artifacts now live in `code/config/` and `code/include/protocols/`.

Everything else should live under a role-specific directory.
