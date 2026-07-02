# kalin-wm UX Polish & User Feedback Review

**Date:** 2026-04-10  
**Scope:** Visual feedback, input feedback, state indication, error handling, and missing polish  
**Reference Compositors:** Niri, Hyprland, Sway

---

## Executive Summary

kalin-wm implements an innovative infinite 2D viewport with hybrid anchoring, but lacks essential UX feedback mechanisms that modern users expect. The compositor functions correctly but communicates poorly with users about state changes, errors, and available actions.

**Key Finding:** Users must rely on external status bars (via `printstatus()`) for all state information, creating a fragmented experience that feels "blind" without additional tooling.

---

## Issues Ranked by Impact

### Critical (Breaks Core Workflow)

| Issue | Impact | Current Behavior |
|-------|--------|------------------|
| **Silent Spawn Failures** | HIGH | `spawn()` uses `fork()`/`execvp()` but child process errors are lost; failed commands give no feedback |
| **No Visual Feedback on Pan/Zoom** | HIGH | Viewport changes instantly with no indication of position or zoom level |
| **Follow Mode State Invisible** | MEDIUM-HIGH | `viewport.follow` and `viewport.follow_new_windows` toggle silently |

### Major (Significant Friction)

| Issue | Impact | Current Behavior |
|-------|--------|------------------|
| **Crop Mode Poorly Signaled** | MEDIUM | Only cursor changes to crosshair; no prominent "CROP MODE" indicator |
| **No Window Title Visibility** | MEDIUM | Window titles exist in Client struct but never rendered |
| **Anchor/Column State Invisible** | MEDIUM | `layout_state.is_anchored` has no visual representation |
| **No Exit Confirmation** | MEDIUM | `quit()` calls `wl_display_terminate()` immediately |

### Minor (Polish & Delight)

| Issue | Impact | Current Behavior |
|-------|--------|------------------|
| **Cursor Doesn't Change for Operations** | LOW | Only crop mode changes cursor; move/resize don't indicate state |
| **No Startup Indication** | LOW | Gray background until first window appears |
| **No Recovery Guidance on Errors** | LOW | Fatal errors call `die()` with message but no troubleshooting help |
| **Zoom Level Not Displayed** | LOW | `viewport.zoom` exists but isn't shown to user |

---

## Detailed Analysis & Recommendations

### 1. Visual Feedback

#### 1.1 Crop Mode Visibility

**Current State:**
```c
// cropbegin() - line 2457
wlr_cursor_set_xcursor(cursor, cursor_mgr, "crosshair");
wlr_log(WLR_INFO, "Crop mode started - drag to select region, Escape to cancel");
```

**Problems:**
- Cursor change is subtle and may be missed
- No persistent on-screen indicator
- Users may forget they're in crop mode
- Only visible feedback is the selection rectangle after dragging starts

**Recommendation - Quick Win:**
Add a prominent overlay indicator in `LyrOverlay` layer:

```c
// Add to crop_editor struct
typedef struct {
    // ... existing fields
    struct wlr_scene_rect *mode_indicator_bg;
    struct wlr_scene_rect *mode_indicator_text;  // Would need text rendering
} CropEditor;

// In cropbegin() - create prominent mode indicator
// Position: Top-center of screen, bright color, cannot be missed
// Mockup description:
// ┌─────────────────────────────────────┐
// │  ☐ CROP MODE  |  Drag to select     │  ← Semi-transparent banner
// │  ☐ Press Esc to cancel              │    at top of screen
// └─────────────────────────────────────┘
```

**Implementation Priority:** HIGH  
**Effort:** Small (add colored rectangle banner, text requires additional library)

---

#### 1.2 Window Focus Indication

**Current State:**
```c
// focusclient() - lines 1552-1570
if (!exclusive_focus && !seat->drag)
    client_set_border_color(c, focuscolor);
// ...
client_set_border_color(old_c, bordercolor);
```

**Problems:**
- Only 1px border color change (blue vs gray from config.h)
- Very subtle on high-DPI displays
- No indication of focus on inactive monitor

**Recommendation - Quick Win:**
```c
// config.h additions
static const int focus_border_width = 3;  /* Thicker border for focused window */
static const float focus_glow[] = COLOR(0x00557780);  /* Semi-transparent glow */

// In focusclient() - temporarily increase border width
// Smooth animation would be ideal but width change is immediate improvement
```

**Niri Reference:**
- Niri uses 4px focus ring by default (configurable)
- Draws behind window to avoid CSD conflicts
- Gradient support for visual appeal

**Implementation Priority:** HIGH  
**Effort:** Small

---

#### 1.3 Anchor Mode Visual Difference

**Current State:**
```c
// client_anchor() - line 2702-2719
c->layout_state.is_anchored = 1;
c->layout_state.column = -1;
// No visual change
```

**Problems:**
- Anchored vs column windows look identical
- Users cannot tell which windows will move during pan
- Violates "Be mindful of invisible state" (Niri principle)

**Recommendation - Medium Effort:**
Add distinct border styling for anchored windows:

```c
// config.h
static const float anchored_border[] = COLOR(0xffaa00ff);  /* Orange border */
static const float column_border[] = COLOR(0x444444ff);    /* Gray border */

// Visual mockup:
// ┌─────────────┐     ┌─────────────┐
// │ ═══════     │     │ ───────     │
// │ ║       ║   │ vs  │ │       │   │
// │ ═══════     │     │ ───────     │
// └─────────────┘     └─────────────┘
//   ANCHORED            COLUMN
//  (thick/orange)     (thin/gray)
```

**Alternative Quick Win:**
Add window position markers on wallpaper grid:
- Small indicator dots at window world positions
- Different color/shape for anchored vs column windows

**Implementation Priority:** MEDIUM  
**Effort:** Medium

---

### 2. Input Feedback

#### 2.1 Pan/Zoom Visual Indication

**Current State:**
```c
// viewport_pan() - line 2870-2880
viewport.x += d[0] / viewport.zoom;
viewport.y += d[1] / viewport.zoom;
if (selmon)
    arrange(selmon);  // Immediate visual update only
```

**Problems:**
- No indication of current viewport position
- No zoom level display
- Users get "lost" in the infinite canvas
- No animation/smoothing on viewport changes

**Recommendation - Multi-tier:**

**Quick Win (Status Bar Integration):**
Extend `printstatus()` output:
```c
// Add to printstatus() - line 2343
printf("%s viewport %.0f %.0f %.2f\n",  // x, y, zoom
    m->wlr_output->name, viewport.x, viewport.y, viewport.zoom);
printf("%s follow %d %d\n",  // follow_mode, follow_new
    m->wlr_output->name, viewport.follow, viewport.follow_new_windows);
```

**Medium Effort (On-screen OSD):**
Brief overlay showing viewport state during/after change:
```
┌─────────────────┐
│  ◎ 100%  │  ··· │  ← Fades in on zoom, fades out after 1s
│  ⌂ 0, 0        │  ← Shows position
└─────────────────┘
```

**Long-term (Mini-map):**
Small overview map showing viewport relative to window positions (like Blender or GIMP).

**Implementation Priority:** HIGH for status bar, MEDIUM for OSD  
**Effort:** Small for status bar, Medium for OSD

---

#### 2.2 Cursor State Feedback

**Current State:**
```c
// moveresize() - line 2210-2234
case CurMove:
    wlr_cursor_set_xcursor(cursor, cursor_mgr, "all-scroll");
    break;
case CurResize:
    wlr_cursor_set_xcursor(cursor, cursor_mgr, "se-resize");
    break;
```

**Problems:**
- Only move/resize change cursor
- No cursor change for pan mode (Super+Shift+arrows)
- No indication when in "follow" mode

**Recommendation - Quick Win:**
```c
// In viewport_pan() activation
wlr_cursor_set_xcursor(cursor, cursor_mgr, "move");  // Or "grabbing" while panning

// Add grab cursor during viewport pan operations
// Could use wlr_cursor_shape_v1 for more options
```

**Implementation Priority:** LOW  
**Effort:** Small

---

### 3. State Indication

#### 3.1 Follow Mode Status

**Current State:**
```c
// viewport_toggle_follow() - line 2933-2943
viewport.follow = !viewport.follow;
// Only log message - user never sees this

// viewport_toggle_follow_new() - line 2948-2955
viewport.follow_new_windows = !viewport.follow_new_windows;
wlr_log(WLR_INFO, "Auto-pan to new windows: %s", ...);  // Log only
```

**Problems:**
- Critical viewport state is invisible
- Users must remember current mode
- Toggles feel unresponsive

**Recommendation - Quick Win:**
Add to `printstatus()` output and extend logging:
```c
void viewport_toggle_follow(const Arg *arg) {
    viewport.follow = !viewport.follow;
    
    // Immediate visual feedback via OSD or status
    wlr_log(WLR_INFO, "Camera follow: %s", 
        viewport.follow ? "ON" : "OFF");
    
    // Force status update
    printstatus();
}
```

**Niri Reference:**
- Niri has explicit "center-focused-column" setting
- All state changes are immediately visible in layout

**Implementation Priority:** HIGH  
**Effort:** Small

---

#### 3.2 Window Count/Positions

**Current State:**
- Windows are tracked in `wl_list clients`
- No visual indication of off-screen windows
- Users can "lose" windows outside viewport

**Recommendation - Medium Effort:**
Add edge indicators for off-screen windows:
```
Screen viewport:
┌─────────────────────────────┐
│                             │
│    ┌─────┐                  │
│    │     │     ▲            │  ← Triangle indicator at edge
│    └─────┘                  │    pointing to window above
│                             │
│         ◄─── ▓              │  ← Arrow indicating window
│                             │    to the left
└─────────────────────────────┘
```

**Implementation:**
- In `rendermon()`, check if any `VISIBLEON(c, m)` windows are outside viewport
- Draw small arrow indicators at screen edges pointing toward off-screen windows

**Implementation Priority:** MEDIUM  
**Effort:** Medium

---

### 4. Error Handling UX

#### 4.1 Spawn Failure Feedback

**Current State:**
```c
// spawn() - line 3548-3554
if (fork() == 0) {
    if (dpy)
        close(wl_display_get_fd(dpy));
    execvp(((char **)arg->v)[0], (char **)arg->v);
    die("dwl: execvp %s failed:", ((char **)arg->v)[0]);  // Only child sees this
}
```

**Problems:**
- Child process errors are invisible to user
- Failed commands appear to do nothing
- No indication of what went wrong

**Recommendation - Medium Effort:**
```c
// Use double-fork technique with pipe for error reporting
void spawn(const Arg *arg) {
    int pipefd[2];
    pipe(pipefd);
    
    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        
        // Try to exec
        execvp(...);
        
        // If we get here, exec failed
        write(pipefd[1], &errno, sizeof(errno));
        close(pipefd[1]);
        _exit(1);
    }
    
    // Parent: check for immediate errors
    close(pipefd[1]);
    
    // Use wl_event_loop to watch pipefd[0] for errors
    // If error received, show OSD notification
    
    // OSD mockup on spawn failure:
    // ┌────────────────────────────┐
    // │  Warning:  Failed to run program │
    // │     "firefox"              │
    // │     No such file           │
    // └────────────────────────────┘
}
```

**Simpler Alternative:**
Use `wlr_log(WLR_ERROR, ...)` which appears in journal if running under systemd:
```c
// At minimum, log spawn attempts
wlr_log(WLR_INFO, "Spawning: %s", ((char **)arg->v)[0]);
```

**Implementation Priority:** HIGH  
**Effort:** Medium (for full solution), Small (for logging)

---

#### 4.2 Crash Dumps & Logging

**Current State:**
- Uses `wlr_log()` with configurable `log_level`
- Fatal errors call `die()` which prints to stderr and exits
- No crash recovery or detailed diagnostics

**Problems:**
- Users don't know where to find logs
- No crash dump for debugging
- Error messages are technical/unclear

**Recommendation - Multi-tier:**

**Quick Win:**
Add startup banner with log location:
```c
// In run() - line 3106
wlr_log(WLR_INFO, "kalin-wm starting...");
wlr_log(WLR_INFO, "Log level: %d (use -d for debug)");
wlr_log(WLR_INFO, "Status output: stdout (pipe to status bar)");
```

**Medium Effort:**
Create structured logging with categories:
```c
// Add to kalin.h
typedef enum {
    KALIN_LOG_INPUT,    // Input events
    KALIN_LOG_VIEWPORT, // Viewport changes
    KALIN_LOG_LAYOUT,   // Layout changes
    KALIN_LOG_CLIENT,   // Window lifecycle
    KALIN_LOG_ERROR,    // Errors
} KalinLogCategory;

// Enable selective debugging
#define KALIN_DEBUG_VIEWPORT 1  // Toggle in config
```

**Long-term:**
- Core dump configuration instructions in README
- Debug mode with verbose diagnostics

**Implementation Priority:** MEDIUM  
**Effort:** Small to Medium

---

### 5. Missing Polish

#### 5.1 Startup Experience

**Current State:**
- Gray background (`rootcolor[] = COLOR(0x222222ff)`)
- No indication that WM is loading
- Can appear "frozen" on slow systems

**Recommendation - Medium Effort:**
Create simple startup screen:
```
┌──────────────────────────────────────┐
│                                      │
│           kalin-wm                   │
│         ─────────────                │
│                                      │
│    Infinite Canvas Tiling WM         │
│                                      │
│  ┌──────────────────────────┐        │
│  │████████████████░░░░░░░░░░│        │  ← Progress bar for
│  └──────────────────────────┘        │    module initialization
│                                      │
│  [Super]+[T]  Open terminal          │
│  [Super]+[P]  Open launcher          │
│  [Super]+[?]  Show help              │
│                                      │
└──────────────────────────────────────┘
```

**Simpler Alternative:**
Just a logo/name on the gray background using `wlr_scene_rect` and text rendering.

**Implementation Priority:** LOW  
**Effort:** Medium

---

#### 5.2 Exit Confirmation

**Current State:**
```c
// quit() - line 2399-2402
void quit(const Arg *arg) {
    wl_display_terminate(dpy);
}
```

**Problems:**
- Immediate exit with no confirmation
- Data loss risk in running applications
- Accidental key press can kill session

**Recommendation - Quick Win:**
```c
void quit(const Arg *arg) {
    // Show confirmation dialog or require key combo
    // Option 1: Require double-press
    static time_t last_quit = 0;
    time_t now = time(NULL);
    
    if (now - last_quit > 2) {
        // First press - show warning
        wlr_log(WLR_INFO, "Press Super+Esc again within 2s to quit");
        // Could show OSD notification
        last_quit = now;
        return;
    }
    
    // Second press within 2s - actually quit
    wl_display_terminate(dpy);
}
```

**Niri Reference:**
- Niri provides `niri msg action quit` for graceful shutdown
- External lockers can intercept and confirm

**Implementation Priority:** MEDIUM  
**Effort:** Small

---

#### 5.3 Window Titles

**Current State:**
```c
// Client struct has title tracking
struct wl_listener set_title;
// updatetitle() callback exists but only updates printstatus()
```

**Problems:**
- Window titles are never displayed
- Hard to identify windows at a glance
- No taskbar-like functionality

**Recommendation - Long-term:**
Add optional title bars or overlay labels:
```c
// config.h option
static const int show_titles = 1;  /* 0 = never, 1 = on hover, 2 = always */

// Visual mockup (on hover):
// ┌─────────────────────────┐
// │  📄 Firefox - GitHub    │  ← Semi-transparent bar
// │  ┌───────────────────┐  │    appears on hover
// │  │                   │  │
// │  │    Window         │  │
// │  │    Content        │  │
// │  └───────────────────┘  │
// └─────────────────────────┘
```

**Challenges:**
- Requires text rendering (cairo/pango or similar)
- CSD vs SSD conflicts
- Significant code addition

**Alternative:**
Leave titles to external status bars (current approach) but improve the protocol:
```c
// Enhanced printstatus() with more window info
printf("windows ");
wl_list_for_each(c, &clients, link) {
    if (VISIBLEON(c, m)) {
        printf("%s:%s:%d:%d ", 
            client_get_title(c),
            client_get_appid(c),
            c->world.x, c->world.y);  // Include position
    }
}
printf("\n");
```

**Implementation Priority:** LOW (leave to external bars)  
**Effort:** High for built-in, Small for protocol extension

---

## Implementation Roadmap

### Phase 1: Quick Wins (1-2 weeks)

1. **Extend `printstatus()` output**
   - Add viewport position, zoom level
   - Add follow mode states
   - Add window positions for external bars

2. **Improve focus indication**
   - Thicker focus border (3px default)
   - Better focus color contrast

3. **Add crop mode banner**
   - Simple colored rectangle at top of screen
   - "CROP MODE - Press Esc to cancel"

4. **Improve logging**
   - Add `wlr_log()` calls for all major actions
   - Startup/shutdown messages
   - Spawn attempt logging

5. **Exit confirmation**
   - Double-press protection
   - Visual feedback on first press

### Phase 2: Medium Improvements (2-4 weeks)

6. **On-screen display (OSD)**
   - Viewport state overlay (fades in/out)
   - Error notifications
   - Mode indicators

7. **Spawn error handling**
   - Pipe-based error reporting
   - OSD notification on failure

8. **Anchor mode visuals**
   - Different border colors for anchored vs column
   - Optional position markers on wallpaper

9. **Edge indicators for off-screen windows**
   - Arrow indicators at screen edges
   - Help users navigate infinite canvas

### Phase 3: Long-term Polish (1-2 months)

10. **Startup screen**
    - Simple loading indicator
    - Keybinding hints

11. **Window titles**
    - Decide: built-in vs external only
    - If built-in: text rendering integration

12. **Animations**
    - Smooth viewport panning
    - Window open/close transitions
    - Focus change animations

13. **Mini-map**
    - Overview of window positions
    - Viewport rectangle overlay

---

## Reference: Niri UX Patterns

### What Niri Does Well

| Feature | Implementation | kalin-wm Equivalent |
|---------|---------------|---------------------|
| **Focus ring** | 4px default, drawn behind windows | 1px border, color only |
| **Layout consistency** | Scrollable tiling, windows don't resize | Infinite 2D canvas |
| **Invisible state** | Careful about hidden state | Anchor/follow modes hidden |
| **Actions apply immediately** | No async delays | Same (good!) |
| **Default config** | Well-commented, helpful defaults | Minimal config.h |
| **Error handling** | IPC protocol for status | stdout status stream |

### Key Niri Principles to Apply

1. **"Be mindful of invisible state"**
   - Anchor mode should be visible
   - Follow mode should be visible
   - Viewport position should be discoverable

2. **"Opening a new window should not affect existing windows"**
   - kalin-wm already handles this well with anchored windows

3. **"The focused window should not move around on its own"**
   - Current behavior is good, but need better indication when it does move

---

## Appendix: Mockup Specifications

### Crop Mode Banner

```
Position: Top-center of active monitor
Size: 300x60px
Background: #cc0000 (red) at 80% opacity
Border: 2px white
Text: "CROP MODE" white, bold, 16px
Subtext: "Drag to select  |  Esc to cancel" white, 12px
```

### Viewport OSD

```
Position: Bottom-right, 20px margin
Size: 150x80px
Background: #000000 at 60% opacity
Border-radius: 8px
Content:
  ┌─────────────┐
  │  ◎ 100%     │  ← Zoom level with icon
  │  ⌂ 0, 0     │  ← Position with icon
  │  ◉ Follow   │  ← Follow mode indicator
  └─────────────┘
Behavior: Fades in on change, fades out after 2s
```

### Error Notification

```
Position: Top-right, 20px margin
Size: 300x100px
Background: #cc0000 at 90% opacity
Border-left: 4px solid #ff6666
Content:
  ┌─────────────────────┐
  │  Warning: Error           │
  │                     │
  │  Failed to spawn    │
  │  "firefox"          │
  │                     │
  │  [Details] [Dismiss]│
  └─────────────────────┘
Duration: Persistent until dismissed
```

---

## Conclusion

kalin-wm has a solid foundation with innovative layout concepts, but needs significant UX polish to feel "production ready." The highest-impact improvements are:

1. **Status visibility** - Extend `printstatus()` for external bars
2. **Error feedback** - Fix silent spawn failures
3. **Visual indicators** - Crop mode banner, better focus indication
4. **State communication** - Make anchor/follow modes visible

These changes would bring kalin-wm in line with modern compositor UX expectations while maintaining its lightweight, configurable philosophy.
