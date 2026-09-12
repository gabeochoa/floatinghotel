#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p output/window-resize
nice -n 10 zig c++ -std=c++23 -O0 -fno-sanitize=undefined -fobjc-arc -fblocks \
    -isystem vendor/afterhours/vendor tests/native/window_resize.mm \
    output/objs/_O2/main/sokol_impl.o \
    -framework Cocoa -framework Metal -framework MetalKit -framework QuartzCore \
    -o output/window-resize/native-test
nice -n 10 python3 -c 'import os, subprocess; subprocess.run(["output/window-resize/native-test"], env=dict(os.environ, MTL_DEBUG_LAYER="1"), timeout=30, check=True)'
nice -n 10 python3 -c 'import os, subprocess; subprocess.run(["output/window-resize/native-test"], env=dict(os.environ, MTL_DEBUG_LAYER="1", FH_TEST_RETINA="1"), timeout=30, check=True)'
