# Deploy — layout refactor

**FULLY DEPLOYED 2026-08-14** — the live session runs
`/nix/store/rmrc3wfq…-kalin-wm`: **layout Phases 0-6** (opacity-persist · graph
removed · rail · grow-push · float-under-cursor · attached overlay · rail+overlay
persistence) + **agent input** (click/key/type) + the **layer-shell output-fallback
fix**. Kept as the record of what the deploys needed and the traps they exposed.
See [[layout-impl]]. (Intermediate 2026-08-13 deploy was `if6rzbma…`, Phases 0-3 +
input only.)

## 0. Phases 4-5 blocker — `binds.conf` needs `overlay-pin` ✅ (done 2026-08-14)

Phase 5's new `overlay-pin` action (bound to `Super+L` in `default_binds.h`) tripped
the bind engine's **coverage check that `die()`s at startup** if a known action is
neither bound nor `unbind`-ed in the on-disk config. Migrated at deploy time: added
`bind Super+l -> overlay-pin` to `~/.config/kalin-wm/binds.conf`, validated every
action against the new binary, and the compositor booted clean. (`float-next` is
IPC-only, no bind — needed nothing.)

## 1. `~/.config/kalin-wm/binds.conf` — migrated (was a startup-abort blocker) ✅

The user's real hand-edited `binds.conf` still bound Phase-1-removed actions
(`link-pick` on `Super+l`; `swap-dir` on `Super+Ctrl+{Left,Right,Up,Down}`), which
a Phase-1+ build's parser rejects → startup abort. Migrated **at deploy time**
(not before — the old build would have choked on the new actions):

- `Super+l -> link-pick` → removed (freed; reserved for the Phase 5 overlay-pin).
- `Super+Ctrl+Left/Right -> swap-dir` → `rail-swap left/right`.
- `Super+Ctrl+Up/Down -> swap-dir` → removed (the rail is 1D).
- Added `Super+Ctrl+h/l -> rail-focus left/right` (discrete focus+frame).

Validated every remaining action against the new binary before restarting
(`bind_actions.c` confirms `focus`/`resize`/`rail-swap`/`rail-focus`). The compositor
booted clean → migration correct.

## 2. The build trap that shipped a stale binary (found + fixed) ✅

**The first rebuild silently installed a pre-refactor binary** (had the connection
graph, no rail/input). Root cause: the kalin-wm flake input is a bare `path:`, which
copies the **whole working tree including the git-ignored `build/` dir**; Nix
normalises every mtime to 1970, so a stale `build/kalin-wm` (from Aug 2) read as
up-to-date and `buildPhase = "make"` relinked+installed **that old binary** instead of
compiling the current sources. Caught before restart by `strings`-checking the built
binary (0 `rail-swap`). Fixed: **`buildPhase = "make clean all"`** (committed to
`flake.nix` with a warning comment) + removed the stale `build/` from the checkout.
The corrected rebuild produced `if6rzbma` (rail-swap ×3, link-pick ×0, input present).
**Lesson for any future `path:`-input flake:** never trust bare `make` in `buildPhase`.

## Still open (not deploy blockers)

- **Push** — both repos (kalin-wm main ~25 ahead, kalin-shell 10 ahead) are local-only;
  push needs the user's `gh` re-auth (`gh auth login -h github.com`).
- **Rail-order persistence** — Phase 6; a restart restores window position/size/crop/
  opacity/camera but not rail linkage (rebuilds from live spawns). Restored windows come
  back free-positioned, not on a rail, until re-spawned.
