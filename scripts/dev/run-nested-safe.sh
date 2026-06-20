#!/bin/sh
# Run kalin-wm nested with input workarounds

echo "=== Nested kalin-wm ==="
echo ""
echo "Input options:"
echo "  A) Stop kanata first (recommended for testing)"
echo "  B) Run with limited input (may not work fully)"
echo "  C) Run normally and click inside window"
echo ""
printf "Choose (A/B/C): "
read choice

case "$choice" in
    a|A)
        echo "Stopping kanata..."
        systemctl --user stop kanata 2>/dev/null || pkill -STOP kanata 2>/dev/null
        sleep 1
        echo "Starting nested compositor..."
        echo "Spawn windows with Super+T"
        echo "Press Ctrl+C to stop, then kanata will restart"
        WLR_BACKENDS=wayland ./build/kalin-wm -d 2>&1 | tee /tmp/nested.log
        echo "Restarting kanata..."
        systemctl --user start kanata 2>/dev/null || pkill -CONT kanata 2>/dev/null
        ;;
    b|B)
        echo "Running with input disabled (use for visual testing only)..."
        WLR_BACKENDS=wayland WLR_LIBINPUT_NO_DEVICES=1 ./build/kalin-wm
        ;;
    c|C|*)
        echo "Running normally..."
        echo "Click inside the window to give it focus"
        echo "Spawn windows with Super+T"
        WLR_BACKENDS=wayland ./build/kalin-wm -d 2>&1 | tee /tmp/nested.log
        ;;
esac

echo ""
echo "Check logs: cat /tmp/nested.log"
