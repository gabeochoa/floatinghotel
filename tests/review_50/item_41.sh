#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
repo_dir=$(mktemp -d /tmp/fh-review-41.XXXXXX)
git -C "$repo_dir" init -q -b main
git -C "$repo_dir" config user.name 'Review 50'
git -C "$repo_dir" config user.email review50@example.invalid
printf 'alpha\nbeta\n' > "$repo_dir/bookmarks.txt"
git -C "$repo_dir" add bookmarks.txt
git -C "$repo_dir" commit -qm Baseline
printf 'alpha\nbeta\ngamma\ndelta\n' > "$repo_dir/bookmarks.txt"
output/floatinghotel.exe "$repo_dir" --test-mode --headless \
  --test-script=tests/review_50/item_41.e2e \
  --screenshot-dir="${FH_EVIDENCE_DIR:-output/screenshots/review-50}" --e2e-timeout=40
