#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
repo_dir=$(mktemp -d /tmp/fh-review-46.XXXXXX)
git -C "$repo_dir" init -q -b main
git -C "$repo_dir" config user.name 'Review 50'
git -C "$repo_dir" config user.email review50@example.invalid
seq 1 80 | sed 's/.*/base line &/' > "$repo_dir/contextual.txt"
git -C "$repo_dir" add contextual.txt
git -C "$repo_dir" commit -qm Baseline
awk '{ if (NR == 30) print "changed line 30"; else print }' "$repo_dir/contextual.txt" > "$repo_dir/next.txt"
mv "$repo_dir/next.txt" "$repo_dir/contextual.txt"
output/floatinghotel.exe "$repo_dir" --test-mode --headless \
  --test-script=tests/review_50/item_46.e2e \
  --screenshot-dir=output/screenshots/review-50 --e2e-timeout=40
