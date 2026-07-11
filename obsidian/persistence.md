# Persistence

- Persistence saves and restores window position, size, and the
  [[connection-graph]] across restarts, so the [[infinite-canvas]] and its
  connections survive a session ending. Own translation unit,
  `code/src/persistence.c` / `code/include/persistence.h`.
- State file: `~/.local/share/kalin-wm/canvas_state.json`, hand-rolled flat
  JSON (own writer + a small non-nesting-safe parser — objects inside the
  top-level `clients`/`connections` arrays must stay flat, no nested
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
  set: assigns the instance, applies any matching saved geometry/size/crop/
  fullscreen/ontop state, and reconnects any saved [[connection-graph]] edge
  to whichever partner has *already* registered this run (order-independent
  — whichever of the two maps second is the one that completes the edge,
  since both sides check the same loaded-connections list on their own
  registration). Returns whether a saved position was applied, so
  `mapnotify()`'s placement fallback (spawn-adjacent, or monitor-center for
  the first window) knows whether to run at all.
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
     "crop_saved_base": 1, "isfullscreen": 0, "isontop": 0}
  ],
  "connections": [
    {"a_appid": "foot", "a_title": "foot", "a_instance": 0,
     "b_appid": "foot", "b_title": "foot", "b_instance": 1}
  ]
}
```

- Client `title` in the save file is the **registered snapshot** (whatever
  the client's title was at map time), not a fresh query at save time —
  `save_client_cb()` must use the same identity the connections array uses,
  or the two arrays key inconsistently and connection restoration silently
  fails to match (a second real bug hit during this rework: title changes
  between registration and save time made the client entries use one title
  and the connection entries use another).
- `persistence_save()` runs on drag-release, on `fitwidth()`/`fitheight()`,
  and at various other geometry-changing points (each calls it directly,
  not on a timer) — see the [[ledger]] for the call sites added as each
  feature landed.

## What isn't handled yet

- No per-monitor scaling/clamping if the monitor resolution changes between
  runs — a saved position/size just applies as-is.
