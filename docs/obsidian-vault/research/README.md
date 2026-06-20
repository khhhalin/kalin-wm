# kalin-wm Research Vault

> Technical research and design documentation for kalin-wm Wayland compositor.

## Quick Navigation

| Area | Description |
|------|-------------|
| [Active Design](./active-design/) | Current design specs, audits, and implementation status |
| [Architecture](./architecture/) | System design patterns and recorded decisions |
| [Comparators](./comparators/) | Research on other compositors (Niri, DriftWM, Zoom) |
| [Protocols](./protocols/) | Wayland protocols and wlroots APIs |
| [Rendering](./rendering/) | GPU rendering, scaling algorithms, and compositing |
| [Performance](./performance/) | Optimization techniques and benchmarks |
| [Reference](./reference/) | Glossaries, APIs, wlroots docs, and quick references |
| [Archive](./_archive/) | Superseded or outdated documents |

---

## Research Areas

### Active Design
Current behavior, UX decisions, stability audits, and exit conditions.

- [Index](./active-design/index.md) — Active behavior and UX decisions
- [Exit Conditions](./active-design/exit-conditions.md) — Definition of done (MVP complete, v1.0 in progress)
- [Stability Audit](./active-design/stability-audit.md) — Crash/stability findings (23 tracked issues)
- [UX Polish](./active-design/ux-polish.md) — Visual feedback, input feedback, state indication
- [Implementation Summary](./active-design/implementation-summary.md) — Sprint summary (2026-04-10)
- [Layout Paradigm](./active-design/layout-paradigm.md) — Column vs anchored windows
- [Navigation](./active-design/navigation.md) — Keyboard navigation scheme
- [Window Placement](./active-design/window-placement.md) — New window placement
- [Overview Mode](./active-design/overview-mode.md) — Zoom-out overview (deferred)
- [Animations](./active-design/animations.md) — Smooth transitions (draft)
- [Crop True-Clipping Plan](./active-design/crop-true-clipping-plan.md) — Root-cause + fix plan
- [Code Operation Pipeline](./active-design/code-operation-pipeline.md) — Execution/read-order structure
- [Memory Audit](./active-design/memory-audit.md) — Memory usage findings
- [Fixes Summary](./active-design/fixes-summary.md) — Applied fixes log

### Architecture
System design patterns and implementation decisions.

- [Design Decisions](./architecture/decisions/) — Active design specs and choices
- [Architecture Overview](./architecture/README.md)

### Comparators
Research on other compositors that influenced kalin-wm.

- [Niri](./comparators/niri/) — Scrollable-tiling column layout
- [DriftWM](./comparators/driftwm.md) — Infinite 2D canvas
- [Zoom Features](./comparators/zoom-features.md) — Compositor zoom implementations

### Protocols
Wayland protocol implementations and wlroots APIs.

- [Protocol Matrix](./protocols/protocol-matrix.md) — Implementation status checklist
- [Protocols Overview](./protocols/README.md) — Protocol status and constraints
- [Wayland Protocols Checklist](./protocols/wayland-protocols-checklist.md) — Expanded protocol notes

### Rendering
GPU rendering techniques and scaling algorithms.

- [Scaling Algorithms](./rendering/scaling-algorithms/) — FSR, NIS, Lanczos, bilinear
- [Scaling Algorithms Overview](./rendering/scaling-algorithms.md)
- [Buffer Management](./rendering/buffer-management.md) — Buffer lifecycle and DMA-BUF
- [Rendering Overview](./rendering/README.md)

### Performance
Optimization patterns and performance considerations.

- [Performance Overview](./performance/README.md)

### Reference
Quick references and glossaries.

- [Technical Definitions Glossary](./reference/definitions.md)
- [Wayland Scaling Glossary](./reference/wayland-scaling-glossary.md)
- [Layout Paradigms](./reference/layout-paradigms.md)
- [API Quick Reference](./reference/api-reference.md)
- [Implementation Guide](./reference/implementation-guide.md)
- [Implementation Notes](./reference/implementation-notes.md)
- [wlroots Scene Graph Scaling](./reference/wlroots/scene-graph-scaling.md)
- [wlroots Fractional Scaling](./reference/wlroots/fractional-scaling.md)

---

## Document Conventions

- `[[filename]]` — Internal wiki links
- `[[filename#heading]]` — Links to specific sections
- `> [!note]` — Callout blocks for important information
- Code blocks include language and context

---

*Last updated: 2026-04-20*
