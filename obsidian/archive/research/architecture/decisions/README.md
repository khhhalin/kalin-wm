# Design Decisions

> Active design specifications and architectural decisions for kalin-wm.

## Layout System

| Decision | Status | Description |
|----------|--------|-------------|
| [Column-Based Layout](./layout-column.md) | Yes Decided | Vertical stacking in scrollable columns |
| [Window Placement](./window-placement.md) | Yes Decided | New windows open in a new right column |
| [Viewport Navigation](./viewport-navigation.md) | Yes Decided | Pan, zoom, and directional focus |

## Canonical References

- Placement behavior: [window-placement.md](./window-placement.md)
- Protocol status: [../../protocols/protocol-matrix.md](../../protocols/protocol-matrix.md)

---

## Decision Records

Each decision document includes:
- **Context** — Why this decision was needed
- **Options Considered** — Alternatives evaluated
- **Decision** — What was chosen
- **Consequences** — Trade-offs and implications
- **Status** — Draft → Decided → Deprecated

---

*See [Implementation Guide](../../reference/implementation-guide.md) for implementation patterns.*
