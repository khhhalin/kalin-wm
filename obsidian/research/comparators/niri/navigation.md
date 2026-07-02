# Niri Navigation

> Moving between windows and columns in Niri.

## Directional Navigation

Move focus by direction:

```rust
// From niri-config/src/lib.rs
pub enum Action {
    FocusColumnLeft,
    FocusColumnRight,
    FocusWindowUp,
    FocusWindowDown,
    FocusColumnFirst,
    FocusColumnLast,
}
```

### Default Bindings

| Key | Action |
|-----|--------|
| `Left` / `H` | Focus column left |
| `Right` / `L` | Focus column right |
| `Up` / `K` | Focus window up (in column) |
| `Down` / `J` | Focus window down (in column) |
| `Home` | Focus first column |
| `End` | Focus last column |

---

## Scroll with Focus

When focusing a column off-screen, the view animates to bring it into view:

```
View before:              After FocusRight:
┌─────┬─────┬─────┐       ┌─────┬─────┬─────┐
│  A  │  B  │  C  │       │  B  │  C  │  D  │
│     │     │     │  →    │     │     │     │
└─────┴─────┴─────┘       └─────┴─────┴─────┘
  ↑ view                    ↑ view scrolls
                             to show C, D

Focus on C, view scrolls
```

---

## Overview Mode

Zoom-out view showing all workspaces:

```
Normal view:              Overview mode:
┌───────────┐            ┌───────────────────────┐
│  Column   │            │ W1 │ W2 │ W3 │ W4   │
│    B      │     →      │┌──┐│┌──┐│┌──┐│┌──┐ │
│           │            ││A │││B │││C │││D │ │
└───────────┘            ││B │││  │││  │││  │ │
                         │└──┘│└──┘│└──┘│└──┘ │
                         └───────────────────────┘
```

### Overview Actions

| Action | Description |
|--------|-------------|
| Click window | Focus and exit overview |
| Click workspace | Focus workspace |
| Drag window | Move between workspaces |
| Scroll | Switch workspaces |

---

## Workspace Navigation

```rust
pub enum Action {
    FocusWorkspace(WorkspaceReference),
    FocusWorkspacePrevious,
    FocusWorkspaceUp,
    FocusWorkspaceDown,
}
```

Workspaces are arranged vertically. Use `Up`/`Down` to switch.

---

*See [Animations](./animations.md) for transition effects.*
