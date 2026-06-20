# Spawn Crash Fix - Summary

## Problem
Compositor crashes when spawning a new terminal with 2+ terminals open and the first one focused.

## Root Cause Analysis
The crash was happening in `resize()` during early client initialization:
1. `createnotify()` creates a new client
2. `applyrules()` or `setmon()` is called
3. `arrange()` → `arrange_columns()` → `resize()` 
4. `resize()` was accessing `c->scene_surface`, borders, etc. before they were fully initialized

## Fixes Applied

### 1. Added NULL checks in `resize()` (dwl.c)
```c
if (!c || !c->mon || !client_surface(c)->mapped || !c->scene || !c->scene_surface)
    return;

if (!c->border[0] || !c->border[1] || !c->border[2] || !c->border[3])
    return;
```

### 2. Added geometry validation in `arrange_columns()` (dwl.c)
```c
if (geo.width <= 0 || geo.height <= 0) {
    wlr_log(WLR_ERROR, "[DEBUG] arrange_columns INVALID GEOMETRY...");
    continue;
}
```

### 3. Fixed client initialization in `createnotify()` (was already done)
```c
c->type = XDGShell;
c->layout_state.is_anchored = 0;
c->layout_state.column = -1;
```

### 4. Added extensive debug logging
- `createnotify`: logs scene_surface creation
- `arrange_columns`: logs each client and resize call
- `resize`: logs entry and early returns

## Test Suite Created

### Unit Tests (`tests/test_client_lifecycle.c`)
12 tests covering:
- Basic client creation
- Window placement in columns
- Multiple window spawn (the crash scenario)
- Resize safety (NULL client, NULL monitor, NULL borders)
- Focus switching
- Zoom division by zero
- World coordinate overflow
- Partial initialization

**Run:** `make test` or `cd tests && ./test_client_lifecycle`

### Integration Test (`tests/test_spawn_crash.sh`)
Static analysis of binary for common crash patterns.

**Run:** `make test-integration` or `cd tests && ./test_spawn_crash.sh`

### Debug Runner (`tests/run-debug.sh`)
Runs compositor with debug logging to trace exact crash location.

**Run:** `cd tests && ./run-debug.sh`

## Testing the Fix

### Automated Tests
```bash
nix develop --command make test
```
Expected: 12 tests passed

### Manual Testing
```bash
# Build with debug logging
nix develop --command make

# Run with debug output
cd tests && ./run-debug.sh

# In the compositor:
# 1. Super+Enter (open terminal 1)
# 2. Super+Enter (open terminal 2)
# 3. Super+Up    (focus terminal 1)
# 4. Super+Enter (open terminal 3 - this was crashing)
```

Expected: No crash, windows placed correctly in columns.

### If Crash Still Occurs
The debug log will show exactly where it fails. Check:
```bash
# Last debug message before crash
grep '\[DEBUG\]' /tmp/kalin-wm-debug.log | tail -20

# Full backtrace with gdb
gdb --batch --ex run --ex bt --args ./kalin-wm
```

## Code Changes

### Files Modified:
1. `dwl.c` - Added safety checks and debug logging
2. `Makefile` - Added test targets

### Files Added:
1. `tests/test_client_lifecycle.c` - Unit tests
2. `tests/test_spawn_crash.sh` - Integration test
3. `tests/run-debug.sh` - Debug runner
4. `tests/README.md` - Testing documentation
5. `docs/incidents/SPAWN_CRASH_FIX.md` - This file

## Architecture Notes

The column placement system uses:
- `world.set` flag to track if a window has been placed
- `column_rightmost_edge()` only considers placed windows
- New windows get default size (60% x 70% of monitor)
- `resize()` has multiple safety checks for uninitialized state

## Next Steps if Issue Persists

1. **Check debug log** - See last `[DEBUG]` message before crash
2. **Run with GDB** - Get full backtrace
3. **Check wlroots version** - Ensure wlroots 0.20 compatibility
4. **Scene graph state** - May need to defer resize until surface is mapped
