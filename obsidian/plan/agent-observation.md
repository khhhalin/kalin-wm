# Agent observation primitive

**Status: PROPOSED (2026-08-13), not implemented.** Design intent captured from a
working session; nothing here is built yet. Enabler track — see [[parallel-tracks]].

## Why

Across a long session the single biggest thing slowing agent work on kalin-wm was
that an agent (Claude / a fleet worker) **cannot observe its own changes on the live
session without disturbing Kalin's**. Concretely, three gaps bit repeatedly:

1. No way to drive/observe a Wayland window — verification of the bar, overlays, and
   layout all reduced to *guessing* from IPC state.
2. No reliable capture of one specific window — `screenshot-window` existed but the
   reply format tripped the caller, and whole-screen `grim`/IPC screenshot grabbed
   the wrong (top) window.
3. No isolation — agent test windows entered Kalin's layout; no stable window id.

## Scope (narrowed by Kalin)

**Priority is observation, not input injection.** The valuable primitive is:

> an agent spawns a window **hidden/minimized** (never enters Kalin's view or layout)
> and **screenshots it at native / high resolution**.

Full input synthesis (`click`/`type`/`key` via `wlr_seat_*`) is a *later, separate*
concern — deliberately deferred. This note is only the observe-without-disturbing
primitive.

## The one real design crux: hidden ≠ rendered

wlroots sends frame callbacks **only to surfaces visible on some output**. A fully
hidden window (scene node disabled / off-camera) gets none → the client stops drawing
→ a capture reads a **stale or blank buffer**.

But the *capture* itself is not the problem: [[screenshot-ui]]'s `screenshot-window`
already renders the client's scene subtree **in isolation**, so it works even when the
window is occluded/off-screen/zoomed out. The only question is whether the client has
**drawn its content**:

- **UI that renders on map/resize** (a bar, a panel, most toolkits) paints its full
  content once at map → last-committed buffer is complete → capture is correct. **This
  is the design-iteration case we care about.**
- **Lazy/animated content** may show only an initial frame. Making a hidden window
  render on demand (drive a frame callback) is a follow-up, out of scope here.

So the primitive is closer than it looks — the gaps are format and scale, not rendering.

## Concrete deltas (proposed)

- **`spawn <cmd> --hidden`** — window maps minimized: excluded from the [[viewport]]
  camera, the layout, **and** the taskbar/[[foreign-toplevel]] (so it never blinks in
  [[overview]]). Gets a **stable id** (the same id the `windows` feed needs — see below).
- **`screenshot-window` → native resolution** — today it captures at display-scale
  (native only at zoom 1). Render the subtree at the **surface's own buffer size**, not
  scaled to the output. This is Kalin's "duża rozdzielczość".
- **`screenshot-window` → clean reply** — one JSON object `{path, w, h}`, no second
  broadcast object trailing it (the double-JSON that broke the caller's parser).
- **Stable window id** — the `windows`/state feed should carry a stable id per window so
  an agent can target one deterministically (`app-id` alone hits the first match).

## Deferred (explicitly not this note)

`focus`/`click <id> <x> <y>`/`move`/`key`/`type` via `wlr_seat_*` — the full agent-drive
suite. Notably `click` must map screen→surface-local through the camera pan/zoom (the
compositor already does this transform for connection-click hit-testing). Separate track.

See also: [[screenshot-ui]] · [[ipc-socket]] · [[parallel-tracks]] · [[roadmap]]
