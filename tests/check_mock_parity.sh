#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
capture_dir=${1:-output/mock-parity/final}
review_repo=${2:?Pass the Afterhours repository containing the Exempt a deliberate pill commit}
mkdir -p "$capture_dir"

run_case() {
    local name=$1
    nice -n 10 output/floatinghotel.exe "$review_repo" --test-mode --headless \
        --test-script="tests/review_focus/$name.e2e" \
        --screenshot-dir="$capture_dir/$name" --e2e-timeout=60 \
        > "$capture_dir/$name.log" 2>&1
}

run_case mock_parity
nice -n 10 python3 tests/check_panel_background.py "$capture_dir/mock_parity/commit.png"
for checker in tree heading controls; do
    nice -n 10 python3 "tests/check_mock_$checker.py" \
        "$capture_dir/mock_parity/commit.json" "$capture_dir/mock_parity/narrow.json"
done
run_case mock_diff
nice -n 10 python3 tests/check_mock_diff.py \
    "$capture_dir/mock_diff/diff_default.json" "$capture_dir/mock_diff/diff_viewed.json" \
    "$capture_dir/mock_diff/diff_folded.json" "$capture_dir/mock_diff/diff_narrow.json" \
    "$capture_dir/mock_diff/diff_split.json"
run_case mock_tree_working
run_case mock_multi
nice -n 10 python3 tests/check_mock_file_spacing.py "$capture_dir/mock_multi/multi_both_folded.json"
printf 'PASS native parity, Viewed state, folding, source tabs, and nested tree scrolling\n'
