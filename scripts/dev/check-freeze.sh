#!/bin/sh
# Check for freeze cause in logs

echo "=== Checking for freeze cause ==="
echo ""

if [ -f /tmp/spawn-test.log ]; then
    LOG=/tmp/spawn-test.log
elif [ -f /tmp/kalin-crash.log ]; then
    LOG=/tmp/kalin-crash.log
else
    echo "No log file found"
    echo "Look for: /tmp/spawn-test.log or /tmp/kalin-crash.log"
    exit 1
fi

echo "Log file: $LOG"
echo ""

echo "Last 30 lines:"
tail -30 $LOG
echo ""

echo "Arrange calls (last 10):"
grep "arrange_columns" $LOG | tail -10
echo ""

echo "Resize calls (last 10):"
grep "resize START" $LOG | tail -10
echo ""

echo "Errors:"
grep -i "error\|fail\|invalid" $LOG | tail -10
echo ""

# Check for infinite loops - if arrange is called many times in a row
ARRANGE_COUNT=$(grep -c "arrange_columns START" $LOG 2>/dev/null || echo "0")
echo "Total arrange calls: $ARRANGE_COUNT"
if [ "$ARRANGE_COUNT" -gt 100 ]; then
    echo "WARNING: Very high number of arrange calls - possible infinite loop!"
fi
