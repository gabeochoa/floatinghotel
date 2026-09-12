#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
binary=${1:-"$PWD/output/floatinghotel.exe"}
mkdir -p output/review-50
run_dir=$(mktemp -d "$PWD/output/review-50/startup-actual.XXXXXX")
for launch in first repeat; do
    FH_NAVIGATION_TIMING=1 FH_NAVIGATION_PREFETCH=1 nice -n 10 "$binary" \
        --test-mode --test-script=tests/startup_actual.e2e \
        --screenshot-dir="$run_dir/$launch" --e2e-timeout=40 > "$run_dir/$launch.log" 2>&1
    grep -q 'NAV before_present visible=0 key=0 .*frontmost=0' "$run_dir/$launch.log"
    grep -q 'NAV presented visible=1 key=1' "$run_dir/$launch.log"
    grep -q 'NAV commit_click_accepted' "$run_dir/$launch.log"
    ! grep -q 'NAV premature_' "$run_dir/$launch.log"
    awk -v launch="$launch" '
        /Process start to main:/ { before_main = $(NF-1) }
        /Pre-graphics init:/ { before_graphics = $(NF-1) }
        /NAV commit_click_accepted/ { printf "%s exec-to-click: %.2f ms\n", launch, before_main + before_graphics + $NF }
        /NAV commit_patch_rendered/ { printf "%s exec-to-patch: %.2f ms\n", launch, before_main + before_graphics + $NF }
    ' "$run_dir/$launch.log"
done
printf 'Startup logs and screenshots: %s\n' "$run_dir"
