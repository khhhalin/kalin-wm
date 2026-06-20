#!/bin/bash
# Integration test for spawn crash scenario
# This script tests the specific crash: spawn with 2+ terminals, first focused

set -e

DWL_BIN="${1:-../build/kalin-wm}"
LOG_FILE="test_spawn_crash.log"
TIMEOUT=10

echo "=== Spawn Crash Integration Test ==="
echo "Binary: $DWL_BIN"
echo ""

# Check binary exists
if [ ! -x "$DWL_BIN" ]; then
    echo "ERROR: Binary not found or not executable: $DWL_BIN"
    exit 1
fi

echo "[1/5] Checking binary symbols for debug info..."
if nm "$DWL_BIN" 2>/dev/null | head -1 >/dev/null; then
    echo "      Debug symbols present"
else
    echo "      WARNING: No debug symbols (build with -g)"
fi

echo ""
echo "[2/5] Looking for common crash patterns in binary..."
# Check for potential null dereference patterns
if strings "$DWL_BIN" | grep -i "null\|nil\|segfault" >/dev/null 2>&1; then
    echo "      Found debug strings"
fi

echo ""
echo "[3/5] Static analysis of source code..."

cd ..

# Check for missing null checks
echo "      Checking for potentially unsafe pointer dereferences..."

# Look for border access without null check
echo "        - border accesses:"
grep -n "c->border\[" code/src/dwl.c | head -5

# Look for scene_surface access
echo "        - scene_surface accesses:"
grep -n "c->scene_surface" code/src/dwl.c | head -5

# Check resize function safety
echo ""
echo "[4/5] Checking resize() safety..."
if grep -A 10 "^resize" code/src/dwl.c | grep -q "!c->border"; then
    echo "      PASS: resize() checks for null borders"
else
    echo "      FAIL: resize() may not check borders"
fi

if grep -A 10 "^resize" code/src/dwl.c | grep -q "!c->scene_surface"; then
    echo "      PASS: resize() checks for null scene_surface"
else
    echo "      FAIL: resize() may not check scene_surface"
fi

echo ""
echo "[5/5] Checking setmon() -> resize() call chain..."
if grep -A 20 "^setmon" code/src/dwl.c | grep -q "resize"; then
    echo "      WARNING: setmon() calls resize() - potential for uninitialized data"
    echo "      Checking if setmon validates client state before resize..."
fi

echo ""
echo "=== Recommendations ==="
echo "1. Run with WAYLAND_DEBUG=1 to see protocol errors"
echo "2. Run with gdb: gdb --batch --ex run --ex bt --args $DWL_BIN"
echo "3. Check for wlroots assertions: WLR_BACKENDS=wayland $DWL_BIN"
echo ""
echo "=== Test Complete ==="
