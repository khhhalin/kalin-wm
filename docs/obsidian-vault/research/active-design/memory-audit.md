# kalin-wm Memory Management Audit Report

**Date:** 2026-04-10  
**Scope:** dwl.c, src/*.c, include/*.h  
**Auditor:** Kimi Code CLI

---

## Executive Summary

The kalin-wm codebase shows **generally good memory management practices** with consistent use of the `ecalloc()` wrapper that handles allocation failures. However, several **memory safety issues** have been identified that require attention.

**Critical Issues:** 1  
**High Priority Issues:** 3  
**Medium Priority Issues:** 5  
**Low Priority Issues:** 4

---

## 1. Client Lifecycle Issues

### 1.1 HIGH: Missing wl_list_init() for Client Links (src/client.c:44, dwl.c:1244)

**Issue:** When a Client is allocated via `ecalloc()`, the `wl_list` links (`c->link` and `c->flink`) are zero-initialized but not explicitly initialized using `wl_list_init()`.

**Code:**
```c
c = toplevel->base->data = ecalloc(1, sizeof(*c));  // Zeroes memory
// c->link and c->flink are zeroed but not initialized
```

**Risk:** While `ecalloc()` zeroes memory and wl_list operations may work with zeroed nodes, this is implementation-dependent. Uninitialized wl_list nodes can cause undefined behavior during list operations.

**Fix Recommendation:**
```c
c = toplevel->base->data = ecalloc(1, sizeof(*c));
wl_list_init(&c->link);
wl_list_init(&c->flink);
```

**Files:** `src/client.c:44`, `dwl.c:1244`, `dwl.c:3974` (XWayland)

---

### 1.2 MEDIUM: Missing Scene Node Destruction on Early Exit (src/client.c:113)

**Issue:** In `unmapnotify()`, the scene node is destroyed via `wlr_scene_node_destroy(&c->scene->node)`, but if the client is destroyed before being mapped (e.g., client crashes early), no cleanup occurs.

**Code:**
```c
void
unmapnotify(struct wl_listener *listener, void *data)
{
    Client *c = wl_container_of(listener, c, unmap);
    // ...
    wlr_scene_node_destroy(&c->scene->node);  // Only called on unmap
}
```

**Risk:** If a client is created but never mapped (crash during initialization), scene nodes created in `mapnotify()` may leak.

**Fix Recommendation:** Ensure cleanup paths exist for partially initialized clients.

---

### 1.3 LOW: Inconsistent Listener Removal Between XDG and X11 (dwl.c:1445-1468, src/client.c:67-90)

**Issue:** The `destroynotify()` function has different cleanup paths for XDGShell vs X11 clients. X11 clients don't remove `commit`, `map`, `unmap`, and `maximize` listeners.

**Code:**
```c
#ifdef XWAYLAND
if (c->type != XDGShell) {
    wl_list_remove(&c->activate.link);
    // ... X11 specific listeners
} else
#endif
{
    wl_list_remove(&c->commit.link);
    wl_list_remove(&c->map.link);
    wl_list_remove(&c->unmap.link);
    wl_list_remove(&c->maximize.link);
}
```

**Risk:** This is likely intentional (X11 surfaces have different lifecycle), but should be verified that X11 clients don't have these listeners registered.

---

## 2. Crop Mode Memory Issues

### 2.1 CRITICAL: Double-Free Risk in cropcancel() (dwl.c:2463-2499, src/crop.c:76-90)

**Issue:** `cropcancel()` destroys scene nodes and sets pointers to NULL, but there's a potential race condition if called multiple times.

**Code:**
```c
void
cropcancel(const Arg *arg)
{
    if (!crop_editor.active) return;  // Guard check
    
    wlr_scene_node_destroy(&crop_editor.overlay->node);
    
    for (int i = 0; i < 4; i++) {
        if (crop_editor.border[i])
            wlr_scene_node_destroy(&crop_editor.border[i]->node);
        crop_editor.border[i] = NULL;  // Set to NULL after free
    }
    // ...
}
```

**Risk:** If `cropcancel()` is called from multiple code paths simultaneously (e.g., keybinding + mouse release), a double-free could occur between the NULL check and the destroy call.

**Mitigation:** The `crop_editor.active` guard provides some protection, but this should be verified thread-safe.

**Fix Recommendation:** Consider using atomic operations or ensuring single-threaded access to crop state.

---

### 2.2 HIGH: Memory Leak on Failed cropbegin() (dwl.c:2405-2460)

**Issue:** If `cropbegin()` fails partway through (e.g., after creating overlay but before creating borders), already-created resources are not cleaned up.

**Code:**
```c
void
cropbegin(const Arg *arg)
{
    // ...
    crop_editor.overlay = wlr_scene_rect_create(...);  // Created first
    // If any of the following fail, overlay leaks
    crop_editor.border[0] = wlr_scene_rect_create(...);
    // ...
}
```

**Note:** The current code doesn't check for NULL returns from `wlr_scene_rect_create()`.

**Fix Recommendation:** Add NULL checks and cleanup on failure:
```c
if (!(crop_editor.overlay = wlr_scene_rect_create(...)))
    return;
```

---

### 2.3 MEDIUM: Uninitialized Crop Editor State (dwl.c:234-246, include/kalin.h:423-432)

**Issue:** The crop_editor state is declared as static global but not all fields are explicitly initialized.

**Code:**
```c
static struct {
    bool active;
    Client *target;
    // ...
    struct wlr_scene_rect *overlay;
    struct wlr_scene_rect *border[4];
    // ...
} crop_editor;
```

**Risk:** Static globals are zero-initialized, but relying on this is fragile. Explicit initialization is safer.

**Fix Recommendation:** Use explicit initialization:
```c
static struct { ... } crop_editor = {0};
```

---

## 3. String Handling Issues

### 3.1 HIGH: Potential NULL Pointer Dereference in applyrules() (src/client.c:428-455, dwl.c:572-600)

**Issue:** `client_get_title()` and `client_get_appid()` can return NULL in certain edge cases, but `strstr()` is called on them without checks.

**Code:**
```c
void
applyrules(Client *c)
{
    appid = client_get_appid(c);   // May return NULL
    title = client_get_title(c);   // May return NULL

    for (r = rules; r < END(rules); r++) {
        if ((!r->title || strstr(title, r->title))  // strstr(NULL, ...) is UB
                && (!r->id || strstr(appid, r->id)))  // strstr(NULL, ...) is UB
```

**Risk:** Calling `strstr()` with a NULL pointer is undefined behavior and will likely crash.

**Fix Recommendation:**
```c
appid = client_get_appid(c) ?: "";
title = client_get_title(c) ?: "";
```

Or add explicit NULL checks before the loop.

---

### 3.2 LOW: client_get_title() Returns Pointer to Potentially Freed Memory (client.h:204-212)

**Issue:** `client_get_title()` returns a pointer to internal wlroots memory that may be freed when the client updates its title.

**Code:**
```c
static inline const char *
client_get_title(Client *c)
{
#ifdef XWAYLAND
    if (client_is_x11(c))
        return c->surface.xwayland->title ? c->surface.xwayland->title : "broken";
#endif
    return c->surface.xdg->toplevel->title ? c->surface.xdg->toplevel->title : "broken";
}
```

**Risk:** Callers must not store this pointer long-term. The code generally uses it immediately (e.g., in `printstatus()`), which is safe.

**Recommendation:** Add documentation comment warning about pointer lifetime.

---

## 4. Buffer Management Issues

### 4.1 MEDIUM: wlr_buffer_lock/unlock Balance in client_set_buffer_scale() (dwl.c:3082-3113)

**Issue:** The `client_set_buffer_scale()` function iterates over scene buffers and modifies them without locking.

**Code:**
```c
void
client_set_buffer_scale(Client *c, float scale)
{
    wl_list_for_each(node, &c->scene_surface->children, link) {
        if (node->type == WLR_SCENE_NODE_BUFFER) {
            buffer = wlr_scene_buffer_from_node(node);
            // No buffer lock/unlock
            wlr_scene_buffer_set_dest_size(buffer, dest_w, dest_h);
            wlr_scene_buffer_set_filter_mode(buffer, ...);
        }
    }
}
```

**Risk:** wlroots scene buffer operations should typically lock/unlock buffers to prevent use-after-free if the buffer is destroyed during processing.

**Fix Recommendation:** Add buffer locking:
```c
struct wlr_buffer *buf = wlr_scene_buffer_get_buffer(buffer);
wlr_buffer_lock(buf);
// ... operations ...
wlr_buffer_unlock(buf);
```

---

### 4.2 MEDIUM: Texture Reference Counting Not Verified (client.h:342-346)

**Issue:** `client_set_scale()` calls wlroots functions that may create/destroy textures, but reference counting is not explicitly verified.

**Risk:** Potential texture leaks or use-after-free if reference counting is incorrect.

**Recommendation:** Verify with wlroots documentation that all texture operations properly handle reference counting.

---

## 5. Common Pattern Issues

### 5.1 MEDIUM: ecalloc Return Values Not Checked (Multiple locations)

**Issue:** While `ecalloc()` internally calls `die()` on allocation failure, some allocations bypass this pattern.

**Safe patterns found:**
- `src/client.c:44`, `src/client.c:785`, `src/client.c:1410` - All use `ecalloc()` ✓
- `src/input.c:374`, `src/input.c:530` - All use `ecalloc()` ✓

**Issue pattern:**
- `src/main.c:61`: Uses raw `malloc()` without NULL check:
```c
startup_cmd = malloc(len);  // No NULL check
if (startup_cmd) {          // Check happens after
    // ...
}
```

**Fix Recommendation:** Use `ecalloc()` or add explicit NULL check immediately after allocation.

---

### 5.2 LOW: Free Before NULL Assignment in Multiple Locations

**Issue:** Several locations free memory before setting the pointer to NULL, which could lead to use-after-free if accessed between free and NULL assignment.

**Safe pattern (most locations):**
```c
free(c);
// c = NULL;  // Not needed here as c is a local variable
```

**Potential issue in wallpaper_cleanup():**
```c
void
wallpaper_cleanup(void)
{
    free(wallpaper.lines);
    wallpaper.lines = NULL;  // Good: NULL after free
    wallpaper.num_lines = 0;
}
```

This pattern is actually correct, but should be verified throughout.

---

### 5.3 MEDIUM: LISTEN_STATIC Macro Creates Un tracked Allocations (kalin.h:98)

**Issue:** The `LISTEN_STATIC` macro allocates a listener that cannot be explicitly freed.

**Code:**
```c
#define LISTEN_STATIC(E, H) do { \
    struct wl_listener *_l = ecalloc(1, sizeof(*_l)); \
    _l->notify = (H); \
    wl_signal_add((E), _l); \
} while (0)
```

**Usage in src/client.c:63:**
```c
LISTEN_STATIC(&popup->base->surface->events.commit, commitpopup);
```

**Risk:** These listeners are freed when the signal is destroyed, but there's no explicit cleanup path. This is generally safe but makes memory tracking difficult.

---

## 6. Use-After-Free Risks

### 6.1 HIGH: Potential Use-After-Free in focusclient() (dwl.c:1519-1594, src/client.c:517-592)

**Issue:** The `focusclient()` function accesses client data after potentially destructive operations.

**Code:**
```c
void
focusclient(Client *c, int lift)
{
    struct wlr_surface *old = seat->keyboard_state.focused_surface;
    // ...
    if ((old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l)) == XDGShell) {
        // old_c could be freed during this loop if client destroys itself
        wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
            wlr_xdg_popup_destroy(popup);
    }
```

**Risk:** If a popup's destroy handler causes the client to be destroyed, `old_c` becomes a dangling pointer.

**Fix Recommendation:** Re-verify `old_c` is still valid after the loop using a generation counter or validity check.

---

### 6.2 MEDIUM: wl_list_remove() After Free Risk in destroynotify() (dwl.c:1445-1468)

**Issue:** Listeners are removed from lists after the client is potentially already freed.

**Code:**
```c
void
destroynotify(struct wl_listener *listener, void *data)
{
    Client *c = wl_container_of(listener, c, destroy);
    wl_list_remove(&c->destroy.link);
    // ... more removes ...
    free(c);  // c is freed here
}
```

**Analysis:** This is actually safe because `wl_list_remove()` only operates on the list nodes, not the containing structure. The `free(c)` happens after all list operations.

---

## 7. Memory Leaks

### 7.1 MEDIUM: KeyboardGroup Leak on Virtual Keyboard (src/input.c:666-680)

**Issue:** When a virtual keyboard is created, a new `KeyboardGroup` is allocated but may not be freed if the virtual keyboard is destroyed.

**Code:**
```c
void
virtualkeyboard(struct wl_listener *listener, void *data)
{
    struct wlr_virtual_keyboard_v1 *kb = data;
    KeyboardGroup *group = createkeyboardgroup();  // Allocates
    // ...
    wlr_keyboard_group_add_keyboard(group->wlr_group, &kb->keyboard);
}
```

**Risk:** If the virtual keyboard is destroyed without the destroy listener being called, the group leaks.

---

### 7.2 LOW: PointerConstraint Leak (src/input.c:371-379)

**Issue:** The destroy listener is set up but error handling could leave the constraint unfreed.

**Safe pattern used:**
```c
PointerConstraint *pointer_constraint = ecalloc(1, sizeof(*pointer_constraint));
pointer_constraint->constraint = data;
wl_signal_add(&pointer_constraint->constraint->events.destroy,
        &pointer_constraint->destroy);
pointer_constraint->destroy.notify = destroypointerconstraint;
```

This is correct as long as the destroy signal is always emitted.

---

## 8. Best Practices Recommendations

### 8.1 Implement a Memory Allocation Wrapper

Consider using `ecalloc()` consistently throughout the codebase. Currently, `src/main.c:61` uses raw `malloc()`.

### 8.2 Add NULL Pointer Checks for strstr() Arguments

The `applyrules()` function should guard against NULL title/appid before calling `strstr()`.

### 8.3 Document Pointer Lifetimes

Functions like `client_get_title()` that return pointers to internal data should have explicit documentation about pointer lifetime.

### 8.4 Initialize All wl_list Nodes

Explicitly call `wl_list_init()` on all list nodes during structure initialization, even if `ecalloc()` is used.

### 8.5 Add Buffer Lock/Unlock in Scaling Functions

The `client_set_buffer_scale()` function should lock buffers during modification.

### 8.6 Consider Valgrind/ASan Integration

Add build options to enable AddressSanitizer and Valgrind integration for automated detection of memory issues.

---

## 9. Summary of Fix Priority

### Critical (Fix Immediately)
1. **dwl.c:2463-2499** - Double-free risk in cropcancel()

### High Priority (Fix Soon)
1. **src/client.c:428-455** - NULL dereference in applyrules() via strstr()
2. **dwl.c:2405-2460** - Memory leak on failed cropbegin()
3. **src/client.c:44, dwl.c:1244** - Missing wl_list_init() for client links

### Medium Priority (Fix When Convenient)
1. **src/input.c:666-680** - KeyboardGroup leak on virtual keyboard
2. **dwl.c:3082-3113** - Missing buffer lock/unlock in client_set_buffer_scale()
3. **dwl.c:1519-1594** - Use-after-free risk in focusclient()
4. **src/main.c:61** - malloc() without NULL check
5. **kalin.h:98** - LISTEN_STATIC untracked allocations

### Low Priority (Documentation/Code Quality)
1. **client.h:204-212** - Document pointer lifetime for client_get_title()
2. **dwl.c:234-246** - Explicit crop_editor initialization
3. **Various** - Free before NULL assignment patterns
4. **dwl.c:1445-1468** - X11/XDG listener cleanup inconsistency

---

## 10. Verification Commands

To verify fixes, use these tools:

```bash
# Build with AddressSanitizer
gcc -fsanitize=address -g -o dwl dwl.c ...

# Run under Valgrind
valgrind --leak-check=full --show-leak-kinds=all ./dwl

# Static analysis with Clang Static Analyzer
scan-build gcc -c dwl.c

# Check for common issues with cppcheck
cppcheck --enable=all --inconclusive src/ dwl.c
```

---

*End of Memory Audit Report*
