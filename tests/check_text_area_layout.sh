#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
capture_dir=${1:-output/text-area-layout}
mkdir -p "$capture_dir"
nice -n 10 output/floatinghotel.exe . --test-mode --headless \
    --test-script=tests/review_focus/text_area_layout.e2e \
    --screenshot-dir="$capture_dir" --e2e-timeout=60 > "$capture_dir/native.log" 2>&1
if grep -E "Layout (overflow|wrap): 'text_area_line'" "$capture_dir/native.log"; then
    printf 'Text-area lines exceeded the input bounds\n' >&2
    exit 1
fi
if grep -F 'query will miss' "$capture_dir/native.log"; then
    printf 'UI test queries skipped pending entities\n' >&2
    exit 1
fi
