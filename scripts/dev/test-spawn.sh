#!/bin/sh
# Test script for spawn crash
# Usage: In TTY, run: ./scripts/dev/test-spawn.sh

echo "Starting kalin-wm..."
echo "Spawn 3 terminals with Super+T"
echo "If it freezes, check /tmp/spawn-test.log after reboot"
./build/kalin-wm -d 2>&1 | tee /tmp/spawn-test.log
