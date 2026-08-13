# Persistence

- Persistence saves and restores window position, size, each client's launch
  command (since 2026-07-17), each monitor's camera pan+zoom (since 2026-08-13),
  and each window's opacity (since 2026-08-13) across restarts, so the
  [[infinite-canvas]], the apps themselves, and the exact view survive a session
  ending — see "Session resurrection" and "Camera persistence" below. Own
  translation unit, `code/src/persistence.c` / `code/include/persistence.h`.
  **The [[connection-graph]] used to be saved/restored too (`connections` array,
  `SavedConnection`, reconnect-on-load); removed 2026-08-13 with the graph
  (layout Phase 1 — [[layout-impl]]). Rail order + overlay attachments return to
  the save file in Phase 6.**
- State file: `~/.local/share/kalin-wm/canvas_state.json`, hand-rolled flat
  JSON (own writer + a small non-nesting-safe parser — objects inside the
  top-level `clients`/`cameras` arrays must stay flat, no nested
  objects/arrays, or the parser's `{`/`}` scan breaks).

## Identity: appid + title is not unique

- Two simultaneously open windows of the same app (two plain `foot`
  terminals) share appid *and* often title, so a saved slot needs a third
  key: `instance`, the spawn-order index of same-appid+title windows this
  run (0 = first spawned). `persistence_register_client()` assigns it via a
  per-appid counter (`next_instance_for()`) and matches against last run's
  saved `instance` the same way, on the assumption spawn order repeats.
- **`identity_key(appid, title)` = appid whenever non-empty, title only as a
  fallback.** Title must never be part of the identity/instance-counting key
  on its own — many apps (a terminal, before its shell renames the window)
  change title shortly after mapping, and depending on exact scheduling that
  rename can land before or after registration runs. Keying on title let the
  *same physical window* hash differently across two otherwise-identical
  runs, splitting one counter into two and silently failing every match.
  This was a real bug hit and fixed during the instance-keying rework: it
  looked like "the 2nd/3rd window of an app never restores," traced to the
  registered clients list simply never containing an entry with the title
  the live window now had.
- `persistence_register_client(void *client)` is the single entry point,
  called once per managed client from `mapnotify()` right after `c->mon` is
  set: assigns the instance and applies any matching saved geometry/size/crop/
  fullscreen/ontop/opacity state. (It used to also reconnect saved
  [[connection-graph]] edges here — removed 2026-08-13 with the graph.) Returns
  whether a saved position was applied, so `mapnotify()`'s placement fallback
  (spawn-adjacent, or cursor/monitor-center for the first window) knows whether
  to run at all.
- `persistence_unregister_client()` (called from `unmapnotify()`) removes
  the bookkeeping entry so a later save doesn't describe a stale pointer.

## The restored-width race

- Setting `c->geom.width/height` from a saved value and calling `resize()`
  isn't enough for a **brand-new** client: its own first non-initial commit
  (finalizing whatever size it natively chose) can arrive *after* the
  restore and silently overwrite it via `commitnotify()`'s
  `client_accept_requested_size()`, since that path is deliberately designed
  to let a client pick its own size. Fixed with a one-shot
  `Client.persist_size_pending` flag: set when a restore applies a saved
  width/height, consumed (skipping exactly that one commit's accept) the
  first time `commitnotify()` sees it, then behaves normally again.

## The `mkdir()` bug that made this silently do nothing

- `persistence_init()`'s directory creation was a single non-recursive
  `mkdir("~/.local/share/kalin-wm")` — fails silently (ENOENT) if
  `~/.local/share` itself doesn't exist yet, which it doesn't on a minimal
  system (confirmed: the [[test-vm]] never had it). Every `fopen()` after
  that failed silently too, so persistence had **never actually written or
  read anything**, the entire time this feature existed, without a single
  error surfacing anywhere. Fixed with a small `mkdir -p`-style helper
  (`mkdir_p()`, mutates the path string in place to null out each `/` in
  turn). The exact same bug, independently, was in `crash_report.c`'s crash-
  log directory creation — fixed the same way, same reason it was found:
  investigating a real segfault with no crash log to show for it.

## Session resurrection (2026-07-17)

- `persistence_respawn_saved()` (public, `persistence.h`) replays every saved
  client's *process* at compositor startup, so a restart brings the apps
  themselves back and `persistence_register_client()` re-places each one as
  it maps. Called once from `run()` (`dwl.c`), right **after** the
  `kalin-apps` tmux session bootstrap — the tmux server must already exist
  and carry this compositor's `WAYLAND_DISPLAY` (see [[spawn]]) or the
  respawned apps can't connect. Best-effort by contract: a bad entry is
  skipped, never fatal to startup; total respawns capped (`RESPAWN_MAX`, 32).
- **Capturing the relaunch command:** at registration, the client's PID comes
  from `wl_client_get_credentials()` on its Wayland socket, and
  `/proc/<pid>/cmdline` is shell-quoted word-by-word into one flat string
  (`RegisteredClient.cmd`, snapshotted like appid/title so save time doesn't
  depend on `/proc` still being readable). Saved as a `"cmd"` field; replayed
  via `tmux new-window -t kalin-apps -- sh -c <cmd>` — `sh` re-splits the
  quoted string into the original argv (args with spaces survive).
- **The foot-server trap:** for foot in server mode every terminal window's
  Wayland client is the *daemon*, so the captured cmdline would be
  `foot --server` — respawning that recreates zero windows. Detected
  (argv[0] basename `foot` + a `--server`/`-s` arg) and substituted with
  `foot -e kalin-term`, which opens a real terminal attached to the tmux
  content-persistence layer (see `plan/persistent-desktop.md`). The session
  *name* isn't recoverable from the compositor side, so it's a fresh
  `kalin-term` session, not a reattach of the exact old one — known limit.
- **Panels are never respawned:** an empty `cmd` means layout-only restore.
  `save_client_cb()` writes `""` when `c->ispanel` — checked at *save* time,
  not capture time, because a panel's backing client maps (and registers)
  before DockedPanel docks it, so `ispanel` is only trustworthy later.
  Defense in depth for the map-to-dock race window: respawn also skips
  appids matching `kalin-*` + `-panel`, and `kalin-launcher` (a transient
  toggle window; respawning it would pop the launcher uninvited).
- **Instance ordering:** entries replay in ascending saved-`instance` order
  (all 0s, then all 1s, …), and each `tmux new-window` client is `waitpid()`ed
  before the next fork (it exits as soon as the window exists — a fast local
  round trip, same pattern as `run()`'s bootstrap) so window-creation order is
  serialized. That's the only lever for making the next run's per-appid
  instance counters line up with the save file; still best-effort — two apps
  racing to *map* can swap instances.
- **Parser limitation, honored:** the flat JSON parser delimits objects by
  scanning raw `{`/`}` (and arrays by `[`/`]`), so a command containing any
  of those characters is dropped at capture (layout-only restore for that
  client) rather than written and corrupting every entry after it.
- Duplicate `(identity,instance)` entries in a corrupt/hand-edited file are
  deduped at replay (earliest file entry wins; the loaded list is built by
  prepending, i.e. reverse file order, so the dedup scan runs *forward*).
- **Save freshness caveat:** `persistence_save()` runs on geometry changes,
  not on unmap — a window closed after the last save is still in the file
  and will be respawned once. Fixing that needs a save on unmap in `dwl.c`
  (after the client leaves the `clients` list), which is outside
  persistence.c.

## Camera persistence (2026-08-13)

- Each monitor's camera (pan `x`/`y` + `zoom`, settled values not the
  animation target) is saved keyed by **output name** — a whole-monitor
  property, so it's a top-level `"cameras"` array of `{output, x, y, zoom}`,
  not a per-client field. `save_cameras()` iterates `mons`;
  `persistence_camera_for_output(name, &x, &y, &zoom)` looks one up.
- **Restore point is `createmon()`** (`modules/output/output.c`), right after
  `m->cam = cam_defaults`: a matching saved camera overwrites both `cam.x/y/zoom`
  *and* `cam.target_x/y/target_zoom`, so `viewport_tick()` doesn't animate away
  from the restored spot. No match (or `zoom <= 0`) leaves the defaults — safe.
  `wlr_output->name` is already populated there (the `monrules` loop keys on it
  too). Lazy-loads the save file on first lookup, like registration does.
- **Why it needed a follow-suppression, not just save+load:** the camera isn't
  persisted for its own sake — the *view* is. But `follow_new_windows`
  (default on) would auto-pan the camera onto each respawned window as it maps
  (`viewport_center_on()` in `mapnotify()`), landing the camera on whichever
  restored window mapped last and undoing the restore. Fix: `mapnotify()` skips
  the follow-new pan for any client whose geometry was restored from the save
  (`has_saved_geom`, the return of `persistence_register_client()`). Fresh
  user-spawned windows still follow. Follow-*focus* needs no such guard — it
  uses `viewport_ensure_visible()` (pan only if off-screen), and the restored
  camera by construction already frames the restored windows, so it's a no-op.
  See [[follow-mode]].
- **Save freshness:** a camera pan/zoom on its own does **not** trigger a save
  (`persistence_save()` fires on window/geometry events and on quit, not on
  camera motion). The camera is captured by whatever save fires
  next — in practice frequent, and always on a clean quit. Verified end-to-end
  on a live nested build: pan to (640,360,1.5) → quit → file holds
  `cameras:[{WL-1,640,360,1.5}]` → reboot → `viewport WL-1 640 360 1.50`.

## Save format

```json
{
  "version": 1,
  "timestamp": 0,
  "clients": [
    {"appid": "foot", "title": "foot", "instance": 0,
     "width": 702, "height": 500, "geom_x": 289.0, "geom_y": 150.0,
     "geom_set": 1, "crop_active": 0, "crop_x": 0, "crop_y": 0,
     "crop_w": 0, "crop_h": 0, "crop_base_w": 702, "crop_base_h": 500,
     "crop_saved_base": 1, "isfullscreen": 0, "isontop": 0,
     "opacity": 1.0000, "cmd": "foot -e kalin-term"}
  ],
  "cameras": [
    {"output": "LVDS-1", "x": 640.00, "y": 360.00, "zoom": 1.5000}
  ]
}
```

(A `"connections"` array once sat between `clients` and `cameras`; removed
2026-08-13 with the [[connection-graph]] — [[layout-impl]]. Rail order +
overlay attachments return here in Phase 6.)

- Client `title` in the save file is the **registered snapshot** (whatever
  the client's title was at map time), not a fresh query at save time —
  `save_client_cb()` uses the registered identity so it stays the stable key
  everything else in the file is matched against. (This was a real bug during
  the instance-keying rework: title changes between registration and save time
  made client entries key inconsistently and restoration silently failed to
  match.)
- `persistence_save()` runs on drag-release, on `fitwidth()`/`fitheight()`,
  and at various other geometry-changing points (each calls it directly,
  not on a timer) — see the [[ledger]] for the call sites added as each
  feature landed.

## What isn't handled yet

- No per-monitor scaling/clamping if the monitor resolution changes between
  runs — a saved position/size just applies as-is.
