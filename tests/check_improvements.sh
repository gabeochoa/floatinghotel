#!/usr/bin/env bash
set -uo pipefail
cd "$(dirname "$0")/.."
mkdir -p output/improvement-checks
passed=0
failed=0
run() {
  local name=$1
  shift
  if "$@" > "output/improvement-checks/$name.log" 2>&1; then
    printf 'PASS %s\n' "$name"
    passed=$((passed + 1))
  else
    printf 'FAIL %s (output/improvement-checks/%s.log)\n' "$name" "$name"
    tail -12 "output/improvement-checks/$name.log"
    failed=$((failed + 1))
  fi
}
for script in tests/e2e_scripts/improvement_*.e2e; do
  run "$(basename "$script" .e2e)" output/floatinghotel.exe /tmp/floatinghotel_test_repo \
    --test-mode --headless --test-script="$script" \
    --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
done
for check in hunk_navigation commit_retry long_lines whitespace visible_whitespace \
  diff_context partial_stage image_diff file_history revision_comparison \
  commit_graph file_summary_jump review_scope review_snapshot async_commit \
  virtual_commit reading_position tooltips commit_message; do
  run "$check" bash "tests/check_$check.sh"
done
run find_scroll bash tests/check_hunk_navigation.sh tests/navigation_scripts/improvement_11_find_scroll.e2e
run full_file bash tests/check_diff_context.sh tests/navigation_scripts/improvement_18_full_file.e2e
run blame bash tests/check_file_history.sh tests/navigation_scripts/improvement_26_blame.e2e
run sticky_context bash tests/check_hunk_navigation.sh tests/navigation_scripts/improvement_46_sticky_context.e2e
for flow in commit_detail_regression context_menu_file commit branch_create diff_selection; do
  run "flow_$flow" output/floatinghotel.exe /tmp/floatinghotel_test_repo \
    --test-mode --headless --test-script="tests/e2e_scripts/flow_$flow.e2e" \
    --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
done
printf '%s passed, %s failed\n' "$passed" "$failed"
test "$failed" -eq 0
