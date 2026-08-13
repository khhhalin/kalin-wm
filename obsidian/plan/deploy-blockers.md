# Deploy blockers — layout refactor (Phases 0-3)

**The layout refactor is merged to LOCAL main but NOT deployed.** The live session
still runs the old (pre-Phase-1) build. Before rebuilding + restarting into a
Phase-1-or-later build, these MUST be handled or the new session breaks. See
[[layout-impl]] for the phases.

## 1. `~/.config/kalin-wm/binds.conf` — stale removed actions (STARTUP ABORT)

The user's **real, hand-edited** `~/.config/kalin-wm/binds.conf` (a plain file, not
home-manager-managed, not in `home-config`) still binds actions that Phase 1 removed:

- line 23: `bind Super+l -> link-pick`  → `link-pick`/`ACT_LINK_PICK` gone
- lines 74-77: `bind Super+Ctrl+{Left,Right,Up,Down} -> swap-dir …` → `swap-dir`/`ACT_SWAP_DIR` gone

A Phase-1+ compositor's bind parser rejects unknown actions and **aborts startup** on
this file (confirmed: a background agent's nested smoke had to use a throwaway
`XDG_CONFIG_HOME` to boot).

**Migrate AT deploy time, not before** — the *old* build (still live) would choke on the
*new* actions if the file is changed while it's running/reloading. The migration, matching
the Phase 2/3 chords:

- `Super+l` → remove (freed; reserved for the Phase 5 overlay-pin) — or leave unbound.
- `Super+Ctrl+Left/Right` → `rail-swap left` / `rail-swap right`.
- `Super+Ctrl+Up/Down` → remove (the rail is 1D).
- Optionally add `Super+Ctrl+h -> rail-focus left` / `Super+Ctrl+l -> rail-focus right`
  (discrete focus+frame of the next/prev rail window).

Do this in the same change that rebuilds + restarts into the new build.

## 2. Nothing pushed / not deployed

Both repos are ahead of their remotes locally only (kalin-wm main, kalin-shell main); the
`home-config` `flake.lock` `kalin-wm` input still points at an older tree. A deploy is a
rebuild against the new tree **plus** the binds.conf migration above **plus** a session
restart — all keeper-gated, on explicit request.
