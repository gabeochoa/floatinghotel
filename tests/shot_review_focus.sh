#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
project_dir="$PWD"
capture_dir="$project_dir/output/screenshots/review-focus"
renderer="$project_dir/output/mock-tools/review-focus-shot"
mkdir -p "$capture_dir" "$(dirname "$renderer")"
if [[ ! -x "$renderer" || tests/wk1_shot.swift -nt "$renderer" ]]; then
    nice -n 10 swiftc -O tests/wk1_shot.swift -o "$renderer"
fi
mock_url="file://$project_dir/docs/mocks/review-focus.html"
nice -n 10 "$renderer" "$mock_url" "$capture_dir/review-desktop.png" 1440 1000
nice -n 10 "$renderer" "$mock_url" "$capture_dir/source-tab.png" 1440 1000 \
    'document.getElementById("source-tab").click();'
nice -n 10 "$renderer" "$mock_url" "$capture_dir/review-zoomed-bottom.png" 1100 760 \
    'setZoom(140); document.querySelector("#tests-file .file-title").click(); document.getElementById("review-scroll").scrollTop = 100000;'
nice -n 10 "$renderer" "$mock_url" "$capture_dir/review-split.png" 1440 1000 \
    'document.getElementById("split").click();'
printf 'Mock screenshots: %s\n' "$capture_dir"
