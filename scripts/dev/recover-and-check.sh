#!/bin/sh
# Recover from freeze and check logs
# Run this from another TTY (e.g., TTY1 or TTY2)

echo "=== Killing kalin-wm process ==="
pkill -9 kalin-wm 2>/dev/null
pkill -9 dwl 2>/dev/null
sleep 1

echo "=== Checking for crash logs ==="
echo ""

# Check dmesg for OOM or segfault
echo "Kernel messages (last 20):"
dmesg 2>/dev/null | tail -20 | grep -i "kill\|oom\|segfault\|dwl\|kalin" || echo "  (none found)"
echo ""

# Check if log exists
if [ -f /tmp/spawn-test.log ]; then
    echo "=== Spawn test log found ==="
    echo ""
    echo "Last 50 lines:"
    tail -50 /tmp/spawn-test.log
    echo ""
    echo "Errors/warnings:"
    grep -i "error\|warn\|fail\|freeze\|loop" /tmp/spawn-test.log | tail -10 || echo "  (none found)"
    echo ""
    echo "Window spawn activity (last 10):"
    grep -E "(createnotify|arrange_columns|resize)" /tmp/spawn-test.log | tail -10 || echo "  (none found)"
else
    echo "No log at /tmp/spawn-test.log"
fi

echo ""
echo "To see full log: cat /tmp/spawn-test.log"
