#!/bin/sh
# Test without heavy debug logging
./build/kalin-wm 2>&1 | tee /tmp/spawn-nodebug.log
