#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
capture_dir=${1:-output/basket-layout}
mkdir -p "$capture_dir"
nice -n 10 output/floatinghotel.exe . --test-mode --headless \
    --test-script=tests/review_focus/basket_layout.e2e \
    --screenshot-dir="$capture_dir" --e2e-timeout=60 > "$capture_dir/native.log" 2>&1
if grep -E "Layout (overflow|wrap): '(basket_|diff_scroll')" "$capture_dir/native.log"; then
    printf 'Feedback basket or adjacent diff viewport overflowed\n' >&2
    exit 1
fi
