#!/bin/bash
# Run kalin-wm with debug logging for spawn crash testing

DWL_BIN="${1:-../build/kalin-wm}"
LOG_FILE="${2:-/tmp/kalin-wm-debug.log}"

echo "=== kalin-wm Debug Runner ==="
echo "Binary: $DWL_BIN"
echo "Log: $LOG_FILE"
echo ""

if [ ! -x "$DWL_BIN" ]; then
    echo "ERROR: Binary not found: $DWL_BIN"
    exit 1
fi

echo "Starting with debug logging..."
echo "Test procedure:"
echo "  1. Wait for compositor to start"
echo "  2. Press Super+Enter to open first terminal"
echo "  3. Press Super+Enter to open second terminal"
echo "  4. Press Super+Up to focus first terminal"
echo "  5. Press Super+Enter to open third terminal (CRASH SCENARIO)"
echo ""
echo "Press Ctrl+C to stop"
echo ""

# Run with debug logging
# WLR_DEBUG=1 for wlroots debug, or just -d for dwl debug
$DWL_BIN -d 2>&1 | tee "$LOG_FILE" | grep -E "(\[DEBUG\]|ERROR|CRITICAL|segfault|Aborted)" || true

echo ""
echo "=== Run Complete ==="
echo "Full log saved to: $LOG_FILE"
echo ""
echo "To analyze:"
echo "  grep '\[DEBUG\]' $LOG_FILE | tail -50"
echo "  grep -i 'error\|fail\|crash' $LOG_FILE"
