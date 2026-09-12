#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
capture_dir=${1:-output/review-spacing}
mkdir -p "$capture_dir"

run_case() {
    local case_name=$1
    nice -n 10 output/floatinghotel.exe . --test-mode --headless \
        --test-script="tests/review_focus/$case_name.e2e" \
        --screenshot-dir="$capture_dir/$case_name" --e2e-timeout=60 \
        > "$capture_dir/$case_name.log" 2>&1
}

run_case spacing_audit
nice -n 10 python3 tests/check_layout_dump.py "$capture_dir/spacing_audit"
nice -n 10 python3 tests/check_control_padding.py "$capture_dir/spacing_audit/"*.json
nice -n 10 python3 tests/check_sidebar_spacing.py \
    "$capture_dir/spacing_audit/spacing_commit.json" \
    "$capture_dir/spacing_audit/spacing_zoom.json"
nice -n 10 python3 tests/check_files_controls_spacing.py \
    "$capture_dir/spacing_audit/spacing_files.json" \
    "$capture_dir/spacing_audit/spacing_small.json"

nice -n 10 bash tests/check_basket_layout.sh "$capture_dir/basket"
nice -n 10 python3 tests/check_feedback_spacing.py "$capture_dir/basket"
run_case working_review
run_case toast_basic
run_case toast_layout
nice -n 10 python3 tests/check_toast_layout.py "$capture_dir/toast_layout"
nice -n 10 bash tests/check_text_area_layout.sh "$capture_dir/text_area"
run_case menu_order
nice -n 10 output/floatinghotel.exe . --test-mode --headless \
    --test-script=tests/e2e_scripts/flow_binary_file.e2e \
    --screenshot-dir="$capture_dir/binary" --e2e-timeout=60 \
    > "$capture_dir/binary.log" 2>&1
nice -n 10 python3 tests/check_binary_layout_dump.py "$capture_dir/binary/binary_layout.json"
run_case commit_splitter

if [[ $(uname -s) == Darwin ]]; then
    nice -n 10 bash tests/check_native_menu.sh
    FH_NATIVE_MENUS=1 run_case native_menu
fi
