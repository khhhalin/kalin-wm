# Deploy — layout refactor (Phases 0-3 + agent input)

**DEPLOYED 2026-08-13.** The live session runs the new build
`/nix/store/if6rzbma…-kalin-wm` (rail + grow-push + opacity-persist + graph
removed + click/key/type). Kept here as the record of what the deploy needed and
the build trap it exposed. See [[layout-impl]].

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
