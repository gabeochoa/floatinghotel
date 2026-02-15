#!/bin/bash
# Run all flow E2E tests in a single app session for speed.
# Usage: ./scripts/run_flow_tests.sh [--no-resize]
set -euo pipefail

cd "$(dirname "$0")/.."

# Create temp dir with just flow tests
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT
cp tests/e2e_scripts/flow_*.e2e "$TMPDIR/"

EXTRA_FLAGS=""
for arg in "$@"; do
    case "$arg" in
        --no-resize) EXTRA_FLAGS="$EXTRA_FLAGS --e2e-no-resize" ;;
    esac
done

rm -f output/screenshots/flow_*.png

./output/floatinghotel.exe \
    --test-mode \
    --test-script-dir="$TMPDIR" \
    --e2e-timeout=120 \
    $EXTRA_FLAGS 2>&1 | grep -E "Screenshot|Script|FAIL|ERROR|finished|passed|failed|====="

echo ""
echo "Screenshots: $(ls output/screenshots/flow_*.png 2>/dev/null | wc -l) captured"
