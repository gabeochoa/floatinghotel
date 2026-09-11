#!/usr/bin/env bash
set -euo pipefail
if [ ! -f "$FLOATINGHOTEL_TEST_MARKER" ]; then
    touch "$FLOATINGHOTEL_TEST_MARKER"
    exit 1
fi
cat "$1"
