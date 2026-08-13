# Parallel tracks (worktree/fleet plan)

**Status: PLANNING (2026-08-13).** A map of the open threads from a working session and
how to run them in parallel. Kalin's model: **each agent gets its own git worktree,
works independently, and branches merge later** (see [[fleet-workflow]] for the mechanics).
Nothing here is started; this is the dispatch map.

## Tracks

| Track | Note | Depends on | Can start now? |
|---|---|---|---|
| **Observation** (enabler) | [[agent-observation]] | — | Yes. Small, self-contained, mergeable alone. |
| **Layout** (core) | [[layout-rethink]] | impl plan | **Model decided** (hybrid rail); needs impl plan, then build. |
| **Bar** (native) | — | stack decision | Design yes; build blocked on stack. |
| **Overlay aesthetics** | [[connection-lines]] variants | **Layout decision** | Blocked — see dependency below. |
| **OCaml exploration** | — | — | Strategic, any time. |

## Ordering / dependencies

- **Observation is independent and highest-ROI** — it fixes the verification bottleneck
  that slowed the whole session, so building it *first* makes every other track cheaper
  to verify. But it does not block anything else from starting.
- **Layout is foundational.** The bar and overlays sit on top of the window model, so the
  [[layout-rethink]] decision (keep [[connection-graph]] or go scrolling-rail) ripples
  outward. In particular:
  - **Overlay aesthetics is gated by Layout.** The connection-line variants (the ✦/•
    constellation and the sparkle/arc/thread alternatives explored this session) only
    matter *if the connection-graph survives*. If [[layout-rethink]] drops it, the lines
    go with it — so do **not** invest in line aesthetics until the layout model is
    decided. Recorded so the mockup work isn't mistaken for a committed direction.
- **Design tracks don't wait on the enabler.** Layout and Bar design can proceed now on
  mockups / a nested compositor (as this session did), then switch to
  `spawn --hidden` + native `screenshot-window` for verification once [[agent-observation]]
  merges. This is the "na raz robić [obserwację] i design" — both at once.

## Bar track — parked decisions

Native **wlr-layer-shell** bar chosen (screen-space → zero camera shift by construction;
instant start). Open: **stack** — the direction moved C → Rust ("one style with the Rust
apps") → then Kalin paused on the whole stack question, weighing a wider "lightweight
quickshell replacement, tightly compositor-bound" and an **OCaml** experiment. v1 scope
agreed as *minimal passive first* (clock + focused title + battery/volume/cpu-mem, no
clicks) — but the whole bar decision is **parked** pending the stack call.

## OCaml exploration (flex)

Kalin wants OCaml somewhere "for flex." Top-3 fit (ranked): (1) the **shell/widget layer**
as OCaml wlr-layer-shell clients via `ocaml-wayland` (which has layer-shell client
bindings) — directly the lightweight-quickshell-replacement idea; (2) a **curation/policy
orchestration daemon** consuming compositor IPC (logic-heavy → OCaml's sweet spot); (3) a
**typed config/rules DSL** compiling to what the C core reads. Bad fit: the compositor
render/DRM hot path (GC). Meta-tension: C + Rust + OCaml = three runtimes for a solo
desktop — cuts against "curated/simple"; picking one non-C language for the shell layer is
cleaner than spreading across both.

See also: [[agent-observation]] · [[layout-rethink]] · [[fleet-workflow]] · [[roadmap]] · [[kalin-wm]]
