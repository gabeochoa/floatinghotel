#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
bash tests/run_unit_tests.sh test_code_gutter
repo_dir=$(mktemp -d /tmp/fh-review-48.XXXXXX)
git -C "$repo_dir" init -q -b main
git -C "$repo_dir" config user.name 'Review 50'
git -C "$repo_dir" config user.email review50@example.invalid
seq 1 120 | sed 's/.*/base line &/' > "$repo_dir/consolidated.txt"
git -C "$repo_dir" add consolidated.txt
git -C "$repo_dir" commit -qm Baseline
awk '{ if (NR == 5) print "after line 5"; else if (NR == 90) print "after line 90"; else print }' "$repo_dir/consolidated.txt" > "$repo_dir/next.txt"
mv "$repo_dir/next.txt" "$repo_dir/consolidated.txt"
output/floatinghotel.exe "$repo_dir" --test-mode --headless \
  --test-script=tests/review_50/item_48.e2e \
  --screenshot-dir=output/screenshots/review-50 --e2e-timeout=40
output/floatinghotel.exe "$repo_dir" --test-mode --headless \
  --test-script=tests/review_50/item_48_single_file.e2e \
  --screenshot-dir=output/screenshots/review-50 --e2e-timeout=40
