# session-resurrection — worker report

- Branch: `worktree-agent-ab85fcd19538e6071`
- Status: for-review. Build green, all unit tests pass (before and after).

## What changed and why

Persistence now captures each client's **launch command** and replays it at
startup, so a compositor restart brings the apps back (not just their
geometry/connections). All changes live in `persistence.c`/`persistence.h`.

**Capture (registration time).** `persistence_register_client()` now calls
`capture_launch_cmd()`: the client's PID via `wl_client_get_credentials()`
on its Wayland socket, argv from `/proc/<pid>/cmdline`, shell-quoted word by
word (`append_shell_word()`, single-quote quoting with `'\''` escapes) into
one flat string, snapshotted in the registry (`RegisteredClient.cmd`) like
appid/title so save time doesn't depend on `/proc` still being readable.

**Why this dodges the foot-server trap.** For foot in server mode the
Wayland client is the daemon, so the raw cmdline would be `foot --server` —
respawning it recreates zero windows. Detected (argv[0] basename `foot` plus
a `--server`/`-s` arg) and substituted with `foot -e kalin-term`, which opens
a real terminal window attached to the tmux content-persistence layer
(`plan/persistent-desktop.md`). Limit: the per-terminal tmux *session name*
is not recoverable from the compositor side, so it's a fresh `kalin-term`
session, not a reattach of that exact window's old session.

**Save format.** One new flat field `"cmd"` on each client entry (written via
the existing `json_escape`, parsed via `json_find_string`; old files without
it load fine → layout-only). Flat-parser limitation honored: a command
containing any of `{}[]` is *dropped at capture* (layout-only restore for
that client) because the hand-rolled parser delimits objects/arrays by
scanning those raw characters — writing them would corrupt every entry after
it. Verified: an arg containing spaces (`'/home/kalin/my docs/doc.pdf'`)
round-trips save → load → `sh -c` intact.

**Panel skipping.** `save_client_cb()` writes `cmd:""` when `c->ispanel` —
checked at *save* time, not capture, because a panel's backing client maps
(and registers) before DockedPanel docks it, so `ispanel` isn't set yet at
registration. Defense in depth for a panel saved inside that map→dock race:
`respawn_skip_appid()` also skips `kalin-*` appids containing `-panel`, and
`kalin-launcher` (a transient toggle window; respawning it would pop the
launcher open uninvited at startup).

**Respawn (`persistence_respawn_saved()`).** Iterates loaded save entries in
ascending saved-`instance` passes (all 0s, then 1s, …) and launches each via
`fork` + `execlp("tmux", "new-window", "-t", "kalin-apps", "-n", <appid>,
"--", "sh", "-c", <cmd>)` — the same path `spawn()` uses, so respawned apps
inherit the tmux server's captured `WAYLAND_DISPLAY`/`KALIN_IPC_SOCKET`.
Each short-lived tmux client is `waitpid()`ed before the next fork (same
pattern as `run()`'s session bootstrap; a fast local round trip) so
window-creation order is serialized — without it the forked clients race and
instance ordering is meaningless. Still best-effort: apps racing to *map*
can swap instances. Skips empty-cmd entries and duplicates (earliest file
entry wins — the loaded list is prepend-built/reverse file order, so the
dedup scan runs forward), capped at `RESPAWN_MAX` (32) against corrupt
files. Never fails startup; errors log and skip.

## Files touched (all within scope)

- `code/src/persistence.c` — capture, save field, parse, respawn.
- `code/include/persistence.h` — `SavedClientState.cmd`,
  `persistence_respawn_saved()` declaration + contract.
- `obsidian/implementation/persistence.md` — new "Session resurrection"
  section, save-format example updated, intro line updated.
- `obsidian/tasks/session-resurrection.md` — **created** (see blockers) and
  set `Status: for-review` + `Branch:`.
- `obsidian/agents/session-resurrection/report.md` — this file.

## Integration call site (keeper wires at the gate)

One line in `code/src/dwl.c`, in `run()`, immediately **after** the
kalin-apps tmux bootstrap block (after the `waitpid(tpid, ...)` block's
closing brace around line 3610, before `wlr_backend_start()`):

```c
persistence_respawn_saved();
```

`persistence.h` is already included by dwl.c. Placement matters: after the
bootstrap so the tmux server exists and carries this compositor's
`WAYLAND_DISPLAY`; `persistence_init()` (line 3582) has already loaded the
save file by then (respawn also self-inits defensively).

Optional follow-up the keeper may want (out of my scope, dwl.c): call
`persistence_save()` in the unmap/destroy path *after* `wl_list_remove(&c->link)`
(~line 4506) — today the save file only refreshes on geometry changes, so a
window closed after the last save is still in the file and respawns once.
Pre-existing staleness, now user-visible via respawn.

## Verification

- Baseline before changes: `nix develop -c make clean all` exit 0;
  `nix develop -c make test-unit` all green (18 + 25 + shader-math suites).
- After changes: `nix develop -c make clean all` exit 0;
  `nix develop -c make test-unit` → `18 passed`, `25 passed`,
  `OK: all checks passed`, 0 assertion failures.
- **Unit tests could not be added to `make test-unit`:** the Makefile and
  `code/tests/` are outside this task's scope. Instead, an ad-hoc integration
  test (scratchpad, not committed) linked the real `persistence.o` against
  stubs, pointed `$HOME` at a temp dir with a crafted `canvas_state.json`
  (7 entries: two foot instances, zathura with a space-containing arg, a
  `kalin-clock-panel-1`, `kalin-launcher`, a no-cmd legacy entry, and a
  duplicate zathura), with a fake `tmux` shim on `$PATH` logging its argv.
  Result: exactly 3 launches in the right order (instance-0 zathura + foot,
  then instance-1 foot), quoting intact, panel/launcher/no-cmd/duplicate all
  skipped. This test caught two real bugs before commit: dedup originally
  kept the *last*-in-file duplicate (list is reverse file order), and
  fire-and-forget forking made creation order racy — both fixed
  (forward-scan dedup; `waitpid` per tmux client).
- **Not run** (keeper-only/serial per task rules): VM tests, `nix build`,
  `nixos-rebuild`. The live end-to-end path (real restart → apps respawn and
  re-place) is untested until the keeper wires the dwl.c call and runs the VM.

## For the keeper to reconcile / blockers

- **`obsidian/tasks/session-resurrection.md` did not exist** — not in this
  worktree nor on `main` — though the fleet prompt referenced it as carrying
  the objective. I reconstructed it from `plan/roadmap.md` ("Session
  resurrection", 2026-07-16) and `plan/persistent-desktop.md` Phase 2 and
  noted the reconstruction inside it. Please verify it matches your intent.
- `plan/roadmap.md` "Session resurrection" under "v1.0 features — open" and
  `plan/persistent-desktop.md` Phase 2 "Auto-relaunch (Level 2 for GUI)"
  should be marked shipped-pending-integration once the dwl.c line is wired
  (plan/ is read-only to me).
- No scope collisions encountered; dwl.c and kalin.h untouched.
- Behavior notes for review: `kalin-scratchpad` *is* respawned (deliberate —
  it's a real terminal; the first `Super+grave` after restart toggles it
  hidden). If the nixos-session wrapper also autostarts a terminal at login,
  a saved terminal respawn could add one extra window — worth a look during
  VM verification.
