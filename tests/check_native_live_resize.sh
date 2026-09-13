#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p output/live-resize
nice -n 10 clang++ -std=c++23 -O0 -fno-sanitize=undefined -fobjc-arc -fblocks \
    -isystem vendor/afterhours/vendor tests/native/live_resize.mm \
    output/objs/_O2/main/sokol_impl.o \
    -framework Cocoa -framework Metal -framework MetalKit -framework QuartzCore \
    -o output/live-resize/native-test
nice -n 10 python3 - <<'PYTHON'
import os
from pathlib import Path
import subprocess
for label, retina in [('native', False), ('retina', True)]:
    env = dict(os.environ, MTL_DEBUG_LAYER='1', FH_RESIZE_TIMING='1')
    if retina:
        env['FH_TEST_RETINA'] = '1'
    with Path(f'output/live-resize/{label}.log').open('w') as log:
        subprocess.run(['output/live-resize/native-test'], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=30, check=True)
    print(Path(f'output/live-resize/{label}.log').read_text())
PYTHON
