# Containerized persistence — podman + a Wayland proxy (design intent)

- **Status: design / investigation only (2026-07-18). Nothing in tree; not
  committed to building.** This note captures a discussed architecture for
  making GUI apps survive a compositor restart, and — critically — records
  *why the obvious approaches don't work* so the fork is settled before any
  code.
- This is the keeper-layer design intent. It **reopens "Level 3 — live GUI
  persistence"**, which [[persistent-desktop]] had rejected; see the
  reconciliation there. As-built persistence today lives in
  [[persistence|implementation/persistence.md]] (respawn-fresh + tmux content).

## The goal and the wall it hits

- Goal: the working environment survives a compositor restart with **app state
  intact** — a browser comes back with its tabs, not as a blank new window.
- The wall: **a Wayland client dies when its compositor dies.** The `wl_display`
  unix socket breaks on compositor exit and GTK/Qt/Electron quit on that EOF.
  Running the app in a **podman** container does *not* change this — the
  container decouples the *process image and filesystem*, but the app inside
  still sees its Wayland connection drop and exits. **Podman is isolation and
  reproducible environments, not process survival.**

## Why checkpoint/restore (CRIU) is out — especially here

- `podman container checkpoint`/`restore` (CRIU) snapshots a process's memory +
  kernel state, but a GUI app holds live references to things *outside* itself
  that all go stale on restore:
  1. **The Wayland socket peer is gone** — CRIU can only reconnect an external
     socket to a peer that still exists; after a restart the old compositor
     process doesn't.
  2. **GPU contexts can't be serialized** — GL/EGL contexts, GEM/dmabuf buffers,
     DRM state live in the kernel driver and on the GPU, not in process RAM.
     CRIU's *only* GPU plugin is **amdgpu**.
  3. **Shared buffers sever** — `wl_shm`/dmabuf buffers are shared with the
     compositor; after restart it no longer holds them.
- **This host is an Intel Ivy Bridge iGPU** (`8086:0166`, 3rd Gen Core). No
  Intel CRIU GPU plugin exists → GPU-app checkpoint/restore is a hard no on this
  machine, not merely fragile. **Rejected.**
- Whole-system hibernate (`systemctl hibernate`) *does* work — it freezes and
  thaws kernel + GPU + compositor + apps atomically, so nothing goes stale — but
  it's all-or-nothing (the whole machine) and does nothing for the
  compositor-restart-while-machine-is-up case that actually matters here.

## The one mechanism that survives a compositor restart: a Wayland proxy

Insert a **stable intermediary display** between each app and the real
compositor. The app binds to the proxy (which never dies); the proxy reconnects
to the new compositor and replays state.

```
app ──(socket A)── proxy ──(socket B)── real compositor
      proxy pretends   (stays alive)     (dies & restarts)
      to be the WM
```

- Proxy runs **inside each container**; socket B is the host compositor socket,
  bind-mounted in. One proxy per app matches "each app in a container."
- When the compositor dies, **socket B breaks but socket A does not** — the app
  never sees EOF, never quits. The proxy absorbs the death. This is the whole
  trick.

### Why it must be a stateful protocol mirror, not a byte-forwarder

- Wayland has **no "give me full state" request** — the protocol is a stream of
  deltas from an initial handshake, and a fresh compositor knows nothing. To
  make windows reappear the proxy must **reconstruct the entire conversation**
  against the new compositor.
- So the proxy parses every message both ways and tracks: bound globals, every
  object and its current state (surface roles, `xdg_toplevel` title/app_id,
  committed geometry), and the **object-ID translation table** between the app's
  IDs and the compositor's. [[waypipe]] already maintains exactly this mirror.

### Reconnect sequence

1. **Quiesce** — stop forwarding; queue any further app requests (app runs on,
   oblivious).
2. **Reconnect** to the new compositor socket (retry loop).
3. **Replay the object tree** — redo the `wl_display`/`wl_registry` handshake,
   re-bind globals, re-create each surface → `xdg_surface` → `xdg_toplevel`,
   re-apply title/app_id.
4. **Re-present content** — the expensive step; two variants below.
5. **Flush** the queued requests and resume transparent forwarding.

### The two hard parts

- **(a) Buffer content — the key tradeoff:**
  - *Retain*: proxy keeps a copy of each surface's last front buffer and
    re-attaches it. For `wl_shm` that's a memcpy; **for GPU dmabuf it means
    copying GPU→CPU every frame** — loses zero-copy, taxes the Ivy Bridge iGPU.
    (This is waypipe's model, costly.)
  - *Force-redraw* (**preferred**): retain nothing. The proxy synthesizes the
    `xdg_surface.configure` cycle + a `frame` callback `done`, making the app
    **repaint from scratch** → it commits a fresh buffer → the window maps. Cost
    is one frame of latency / a possible single-frame blank, in exchange for
    dropping buffer-retention and the GPU-copy problem entirely. Invisible for
    nearly every app. **This is what makes the proxy cheap.**
- **(b) Serial / configure resync** — the new compositor's serials start fresh;
  an in-flight `configure`/`ack_configure` straddling the reconnect desyncs. The
  proxy must finish or fabricate a clean configure cycle and synthesize any
  `frame` callback the app is blocked on, or the app stalls.

## Where a kalin-wm protocol fits ("captured via the protocol")

On replay the new compositor sees a fresh `xdg_toplevel` and must drop it at its
**saved world position** and reattach it to the [[connection-graph]]:

- **Reuse path**: replay the same appid/title so
  [[persistence|persistence.c]]'s existing appid+title+`instance` matching
  re-places it for free — but inherits the instance-ordering races that note
  documents.
- **Protocol path (the point of "captured via the protocol")**: a small custom
  Wayland protocol where the proxy tags each surface with a **durable
  persistence token**, and the compositor keys on that directly — deterministic
  re-association, no heuristic. **The protocol is not the persistence mechanism;
  it is the exact re-association channel** that makes replayed windows land
  where they were. Would land as a module under `code/src/modules/protocols/`
  per the [[roadmap]] rule.

## Cost, honestly

- [[waypipe]] gets ~70% — protocol parsing, object tracking, ID translation,
  buffer serialization — but **tears down on disconnect**; it does not survive
  server death. The work is a waypipe fork adding: survive-B-death,
  replay-to-fresh-server, and configure/frame resync. A **substantial new
  subsystem** (a persistent display proxy), not a small feature.
- Per-frame overhead is near-zero **if** the force-redraw variant is taken
  (no buffer retention). The retain variant is the expensive one.
- It composes cleanly with per-app podman containers (one proxy each) and reuses
  the existing persistence identity system for placement.
- It is, on Intel, the **only** path that makes GUI apps genuinely survive a
  compositor restart — the alternative is to accept respawn-fresh for GUI apps
  and rely on each app's own state restore (browser tab restore, tmux for
  terminals), which is today's [[persistent-desktop]] Level 2.

## Open decisions (before any build)

- Force-redraw vs retain buffers (leaning force-redraw).
- Reuse appid+title+instance vs a new durable-token protocol (leaning protocol,
  for determinism).
- One proxy per app vs one multiplexing proxy (leaning per-app, matches
  containers).
- Which apps go in podman at all, and how the Wayland socket + `/dev/dri` +
  XDG runtime get passed into each container.
- Whether this is worth building versus staying at [[persistent-desktop]]
  Level 2 — this note deliberately does **not** commit to building it.

See also: [[persistent-desktop]] · [[persistence]] · [[protocols]] ·
[[connection-graph]] · [[dev-restart]]
