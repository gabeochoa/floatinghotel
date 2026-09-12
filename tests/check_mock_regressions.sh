#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
capture_dir=${1:-output/mock-parity/regressions}
mkdir -p "$capture_dir"
flow_scripts=$(mktemp -d "$capture_dir/flows.XXXXXX")
cp tests/e2e_scripts/flow_*.e2e "$flow_scripts/"
nice -n 10 output/floatinghotel.exe tests/fixture_repo --test-mode --headless \
    --test-script-dir="$flow_scripts" --screenshot-dir="$capture_dir/legacy" \
    --e2e-timeout=60 > "$capture_dir/legacy.log" 2>&1
printf 'PASS legacy browsing flows\n'
nice -n 10 bash tests/check_text_highlight.sh "$capture_dir/highlight" > "$capture_dir/highlight.log" 2>&1
printf 'PASS text selection and find geometry\n'
nice -n 10 bash tests/check_text_flicker.sh "$capture_dir/flicker" > "$capture_dir/flicker.log" 2>&1
printf 'PASS idle text stability\n'
nice -n 10 bash tests/check_commit_graph.sh "$capture_dir/graph" > "$capture_dir/graph.log" 2>&1
printf 'PASS branching and merge graph\n'
nice -n 10 bash tests/review_50/item_14.sh > "$capture_dir/large-message.log" 2>&1
printf 'PASS large commit message\n'
nice -n 10 bash tests/check_review_spacing.sh "$capture_dir/spacing" > "$capture_dir/spacing.log" 2>&1
printf 'PASS spacing, feedback, toasts, splitter, and native menu checks\n'
