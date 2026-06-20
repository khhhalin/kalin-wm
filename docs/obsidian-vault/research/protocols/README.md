# Protocols

> Wayland protocol implementations and wlroots API documentation.

## Source of Truth

- **Canonical protocol status:** [Protocol Matrix](./protocol-matrix.md)
- **Detailed companion notes:** [Wayland Protocols Checklist](../wayland-protocols-checklist.md)

## Protocol Matrix

Quick reference of all protocol implementations.

| Status | Count | Description |
|--------|-------|-------------|
| Essential | 15/15 | Core functionality complete |
| Recommended | 18/21 | 3 blocked on wlroots upgrade |
| Optional | 0/9 | Nice to have, not critical |

See [Protocol Matrix](./protocol-matrix.md) for full details.

---

## Core Protocols

Essential Wayland protocols for basic operation.

- [Protocol Matrix](./protocol-matrix.md#essential-protocols-1515-) — Surface, shell, seat, data-device and more

## Scaling Protocols

HiDPI and fractional scaling support.

- [Fractional Scale (wlroots)](../reference/wlroots/fractional-scaling.md) — `wp_fractional_scale_v1`

## Buffer Protocols

Buffer sharing and DMA-BUF.

- [Buffer Management](../rendering/buffer-management.md) — Zero-copy buffer sharing and DMA-BUF

## Session Protocols

User session management.

- [Protocol Matrix](./protocol-matrix.md#essential-protocols-1515-) — Session lock, idle, output management

## Blocked Protocols

Require wlroots 0.21+ (currently on 0.20):

| Protocol | Purpose | Blocked On |
|----------|---------|------------|
| `ext_foreign_toplevel_list_v1` | Taskbar window lists | wlroots 0.21+ |
| `ext_image_copy_capture_manager_v1` | Screen capture | wlroots 0.21+ |
| `zwp_text_input_manager_v3` | IME/CJK input | wlroots 0.21+ |

---

## Protocol Implementation Pattern

```c
// 1. Include header
#include <wlr/types/wlr_example_protocol_v1.h>

// 2. Create in setup()
void setup(void) {
    wlr_example_protocol_v1_create(dpy);
}
```

---

*See [wlroots protocol headers](https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/master/protocol) for reference.*
