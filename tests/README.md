# Test Suite for kalin-wm

## Unit Tests

### test_client_lifecycle.c
Tests client lifecycle without wlroots dependencies:
- Basic client creation
- Window placement in columns
- Multiple window spawn scenarios
- Placement invariants (already-placed windows are not repositioned)
- Gap/column consistency across sequential spawns
- Resize safety (NULL checks)
- Resize success path with fully initialized client state
- Focus switching
- The specific crash scenario (spawn with 2+ existing)

**Run:**
```bash
make test-unit
```

**Run with coverage:**
```bash
make test-unit-coverage
```

## Integration Tests

### test_spawn_crash.sh
Static analysis script that checks for common crash patterns in the binary.

**Run:**
```bash
cd tests
./test_spawn_crash.sh
```

## Debug Logging

The compositor now has extensive debug logging. Run with:

```bash
# Debug output to console
./kalin-wm -d 2>&1 | grep "\[DEBUG\]"

# Or full debug
WLR_DEBUG=1 ./kalin-wm 2>&1 | tee debug.log
```

Look for these key log messages during spawn:
- `[DEBUG] createnotify START` - New client being created
- `[DEBUG] createnotify scene_surface created` - Scene surface setup
- `[DEBUG] arrange_columns START` - Layout being calculated
- `[DEBUG] arrange_columns calling resize` - About to resize a window
- `[DEBUG] resize START` - Resize function entered
- `[DEBUG] resize EARLY RETURN` - Resize aborted due to safety check

## Safety Checks Added

1. **resize() function:**
   - Checks `!c` (NULL client)
   - Checks `!c->mon` (NULL monitor)
   - Checks `!client_surface(c)->mapped` (surface not ready)
   - Checks `!c->scene` (NULL scene tree)
   - Checks `!c->scene_surface` (NULL scene surface)
   - Checks `!c->border[i]` for all borders
   - Validates geometry (width/height > 0)

2. **arrange_columns() function:**
   - Validates geometry before calling resize
   - Skips windows with invalid dimensions

3. **createnotify() function:**
   - Initializes `c->type = XDGShell` (was missing!)
   - Initializes `c->layout_state` fields

## The Specific Crash Scenario

**Trigger:** Spawn new terminal when 2+ terminals exist and first is focused

**Root Cause:** `resize()` was being called before scene tree was fully initialized

**Fix:** Added early return checks in `resize()` for all uninitialized state

## To Reproduce the Fix Test

1. Build with debug logging:
   ```bash
   nix develop --command make
   ```

2. Run the compositor:
   ```bash
   ./kalin-wm -d 2>&1 | tee /tmp/dwl.log
   ```

3. In the compositor:
   - Open terminal: Super+Enter
   - Open second terminal: Super+Enter
   - Focus first: Super+Up
   - Open third: Super+Enter

4. Check log - should see:
   - No crashes
   - `[DEBUG] resize START` messages
   - Windows placed correctly

## If Crash Still Occurs

The debug log will show exactly where it fails. Look for:
- Last `[DEBUG]` message before crash
- Whether it's in `resize`, `arrange_columns`, or elsewhere
- Whether any `EARLY RETURN` messages appear

Run with GDB for backtrace:
```bash
gdb --batch --ex run --ex bt --args ./kalin-wm
```
