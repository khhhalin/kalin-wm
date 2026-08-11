# Niri Window Placement

> How Niri places new windows in the scrollable layout.

## Default Behavior

New windows open in a **new column to the right** of the currently focused column:

```
Before:                    After opening D:
┌─────┬─────┐              ┌─────┬─────┬─────┐
│  A  │  B  │  ← focused   │  A  │  B  │  D  │
│     │     │              │     │     │     │
└─────┴─────┘              └─────┴─────┴─────┘
                                  ↑
                            New column inserted
```

---

## Opening in Current Column

With `open-window-in-column = true`, new windows stack in the current column:

```
Before:                    After opening C:
┌───────────┐              ┌───────────┐
│     A     │              │     A     │
├───────────┤              ├───────────┤
│     B     │  ← focused   │     B     │
└───────────┘              ├───────────┤
                           │     C     │  ← new
                           └───────────┘
```

---

## Column Width Modes

```rust
pub enum ColumnWidth {
    Fixed(Resize),           // Fixed pixel width
    Proportion(Resize),      // Proportional to output
}

pub enum Resize {
    Fixed(u16),              // Specific value
    Current,                 // Use current column's width
}
```

### Examples

| Configuration | Result |
|---------------|--------|
| `Fixed(800)` | All columns 800px wide |
| `Proportion(0.5)` | Each column half output width |
| `Current` | Match existing column width |

---

## Column Gaps

```
Column gap: 10px between columns
Window gap: 8px between windows in column

┌─────┐    ┌─────┐
│  A  │10px│  B  │
│     │    │     │
└─────┘    └─────┘

Within column:
┌───────┐
│   A   │
├───────┤ 8px
│   B   │
└───────┘
```

---

*See [Navigation](./navigation.md) for moving between columns.*
