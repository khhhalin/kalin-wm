# Deploy — layout refactor

**Phases 0-3 + agent input: DEPLOYED 2026-08-13** — the live session runs
`/nix/store/if6rzbma…-kalin-wm` (rail + grow-push + opacity-persist + graph
removed + click/key/type). **Phases 4-5 (float + overlay): merged to local main,
NOT yet deployed** — see the new blocker below. See [[layout-impl]].

## 0. Phases 4-5 deploy blocker — `binds.conf` needs `overlay-pin` (NOT yet deployed)

Phase 5 adds a new action `overlay-pin` (bound to `Super+L` in `default_binds.h`).
kalin-wm's bind engine runs a **coverage check that `die()`s at startup** if any known
action is neither bound nor `unbind`-ed in the on-disk config. The user's live
`~/.config/kalin-wm/binds.conf` doesn't mention `overlay-pin`, so a Phase-4/5 build
would **abort at startup** on it. Before deploying Phases 4-5, add to that file:

- `bind Super+l -> overlay-pin`   (arm-then-click: focus child, click host)

**AT deploy time only** — the currently-live build (`if6rzbma`) has no `overlay-pin`
action, so adding the line sooner would abort *it* on a reload/restart. Same timing
rule as the Phase-1 `binds.conf` migration below. (`float-next` is IPC-only, no bind,
so it needs nothing.)

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
