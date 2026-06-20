#!/bin/sh
# Run kalin-wm with crash logging
# Usage: ./scripts/dev/run-with-logging.sh
# After crash, check: cat /tmp/kalin-crash.log

echo "Starting kalin-wm with logging..."
echo "After it crashes, run: cat /tmp/kalin-crash.log"
echo ""

# Run compositor and capture all output
./build/kalin-wm -d 2>&1 | tee /tmp/kalin-crash.log

echo ""
echo "Compositor exited. Check logs with:"
echo "  cat /tmp/kalin-crash.log"
