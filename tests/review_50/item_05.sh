#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
bash tests/run_unit_tests.sh test_diff_metrics
metrics_repo=$(mktemp -d /tmp/fh-diff-metrics.XXXXXX)
git -C "$metrics_repo" init -q -b main
printf 'before\n' > "$metrics_repo/source.cpp"
git -C "$metrics_repo" add source.cpp
git -C "$metrics_repo" -c user.name=Test -c user.email=test@example.invalid commit -qm baseline
printf 'after\n' > "$metrics_repo/source.cpp"
printf 'untracked source\n' > "$metrics_repo/new.cpp"
ln -s missing-target "$metrics_repo/link"
output/floatinghotel.exe "$metrics_repo" --test-mode --headless \
  --test-script=tests/review_50/item_05.e2e \
  --screenshot-dir=output/screenshots/review-50 --e2e-timeout=40
bash tests/check_visible_whitespace.sh
