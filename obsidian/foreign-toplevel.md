# Foreign toplevel

- kalin-wm implements `wlr-foreign-toplevel-management-v1`, the Wayland protocol that lets an external program enumerate and control windows.
- It is implemented in the `foreign_toplevel.c` runtime module.

- Through this protocol the [[quickshell-shell]] gets the live window list.
- Through this protocol the [[quickshell-shell]] can activate, close, and fullscreen windows, and capture window thumbnails for [[overview-mode]] and window peek.

- Foreign-toplevel carries the window list; the [[ipc-socket]] carries the [[viewport]] camera state.
- The shell's `CompositorService` chooses the kalin-wm backend (this protocol) when `$KALIN_IPC_SOCKET` is set, otherwise it falls back to a niri backend.

## Thumbnail capture (hyprland-toplevel-export-v1)

- Window thumbnails (Overview grid, taskbar hover-peek) are a *separate*
  protocol, `hyprland-toplevel-export-v1`, hand-implemented in
  `code/src/modules/protocols/toplevel_export.c` — Quickshell's
  `ScreencopyView` is hard-locked to this exact protocol client-side
  (confirmed by disassembly, not just docs), not the more modern
  `ext-image-copy-capture-v1`.
- **Reworked 2026-07-09** to capture straight from the target client's own
  `wlr_surface_get_texture()` rather than rendering the on-canvas region
  through a scratch scene-output (the old approach, modeled on
  `capture.c`'s whole-screen screenshot technique). Two reasons:
  - **Perf**: one blit instead of a full scene render plus a second blit.
  - **Correctness for minimized windows**: `setminimized()` disables the
    client's scene node, so a scene-graph render of a minimized window
    produces nothing. Reading the surface's texture directly bypasses the
    scene graph entirely, so a minimized window's last frame is still
    capturable — this is what makes "peek at a minimized window" actually
    work, not just windows currently on-canvas.
  - Trade-off: thumbnails show raw surface content only, no kalin-wm
    decorations (border/focus ring) or subsurfaces outside the main
    surface. Acceptable at thumbnail scale.
  - Backend-agnostic by construction: `wlr_surface_get_texture()` normalizes
    both `wl_shm` (CPU-rendered clients) and `linux-dmabuf` (GPU-rendered
    clients) into the same `wlr_texture*` — no per-client-backend special
    casing needed for "should work for every client."
  - Destination-side still branches on Quickshell's buffer type: a GPU
    render-pass blit for dmabuf destinations (the fast, common path), or a
    CPU nearest-neighbor scale-and-copy (`read_and_scale_to_cpu_buffer()`)
    when only CPU access is available — `wlr_texture_read_pixels()` has no
    scaling of its own, and the source texture's native size essentially
    never matches the destination (c->geom includes kalin-wm's border, the
    raw surface doesn't).
- See [[quickshell-shell]] for the earlier, unrelated crash this same file
  was touched for (null `captureSource` in the QML timer, not a capture.c
  issue at all).

## Clicking a minimized window's taskbar icon didn't unminimize it (fixed 2026-07-09)

- `ftl_request_activate()` (`code/src/modules/foreign_toplevel.c`, fired when
  Quickshell's taskbar calls `toplevel.activate()` on click) only called
  `focusclient(c, 1)` — it never checked `c->minimized`. A minimized
  client's scene node is disabled by `setminimized()` (see the
  [[quickshell-shell]] gesture/minimize note and `dwl.c`), so "focusing" it
  this way was a no-op visually: nothing reappeared, the window just stayed
  hidden despite being nominally focused.
- Fixed: check `c->minimized` first and call `setminimized(c, 0)` instead of
  `focusclient()` directly when it's set — `setminimized(c, 0)` already
  calls `focusclient(c, 1)` internally when unminimizing, so this covers
  both cases without duplicating that call.
