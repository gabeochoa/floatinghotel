#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
capture_dir=${1:-output/text-flicker/check}
mkdir -p "$capture_dir"
capture_dir=$(mktemp -d "$capture_dir/run.XXXXXX")
nice -n 10 output/floatinghotel.exe . --test-mode --headless \
    --test-script=tests/review_focus/text_flicker.e2e \
    --screenshot-dir="$capture_dir" --e2e-timeout=60 > "$capture_dir/native.log" 2>&1
nice -n 10 python3 tests/check_text_flicker.py "$capture_dir"
printf 'Frame captures: %s\n' "$capture_dir"
