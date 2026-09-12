#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
review_repo=${1:?Pass the repository used for the review-focus comparison}
capture_dir=${2:-output/review-focus/after}
mkdir -p "$capture_dir"
if [[ ${FH_SKIP_BUILD:-0} != 1 ]]; then
    nice -n 10 make -j1 OPT=-O2 > "$capture_dir/build.log" 2>&1
fi
for scenario in capture tabs zoom; do
    nice -n 10 env FH_NAVIGATION_PREFETCH=1 output/floatinghotel.exe "$review_repo" \
        --test-mode --headless --test-script="tests/review_focus/$scenario.e2e" \
        --screenshot-dir="$capture_dir" --e2e-timeout=90 \
        > "$capture_dir/$scenario.log" 2>&1
    printf 'PASS %s\n' "$scenario"
done
nice -n 10 git diff --check
