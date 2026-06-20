#!/bin/sh
# Analyze crash log
# Usage: ./scripts/dev/check-crash.sh

echo "=== Crash Log Analysis ==="
echo ""

if [ ! -f /tmp/kalin-crash.log ]; then
    echo "No crash log found at /tmp/kalin-crash.log"
    echo "Run ./scripts/dev/run-with-logging.sh first"
    exit 1
fi

echo "Last 50 lines of log:"
echo "---"
tail -50 /tmp/kalin-crash.log
echo ""

echo "Looking for errors..."
echo "---"
grep -i "error\|segfault\|abort\|fatal" /tmp/kalin-crash.log | tail -20
echo ""

echo "Window spawn activity:"
echo "---"
grep -E "(createnotify|arrange_columns|resize START)" /tmp/kalin-crash.log | tail -20
echo ""

echo "To see full log: cat /tmp/kalin-crash.log"
