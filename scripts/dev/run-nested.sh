#!/bin/sh
# Run kalin-wm nested (inside current Wayland session)
# This creates a window that runs the compositor

# Stop kanata temporarily if needed
echo "To avoid kanata intercepting keys, you can either:"
echo "  1. Stop kanata temporarily: systemctl --user stop kanata"
echo "  2. Or use mouse to click inside the nested window first"
echo ""

# Run nested with Wayland backend
# The WLR_BACKENDS=wayland forces nested mode
WLR_BACKENDS=wayland WLR_LIBINPUT_NO_DEVICES=1 ./build/kalin-wm
