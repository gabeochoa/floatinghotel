#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
repo_dir=$(mktemp -d /tmp/fh-review-47.XXXXXX)
git -C "$repo_dir" init -q -b main
git -C "$repo_dir" config user.name 'Review 50'
git -C "$repo_dir" config user.email review50@example.invalid
printf 'base\n' > "$repo_dir/a.txt"
git -C "$repo_dir" add a.txt
git -C "$repo_dir" commit -qm Baseline
printf 'base\none\n' > "$repo_dir/a.txt"
printf 'two\n' > "$repo_dir/b.txt"
printf 'three\n' > "$repo_dir/c.txt"
git -C "$repo_dir" add a.txt b.txt c.txt
git -C "$repo_dir" commit -qm 'Overview fixture'
output/floatinghotel.exe "$repo_dir" --test-mode --headless \
  --test-script=tests/review_50/item_47.e2e \
  --screenshot-dir=output/screenshots/review-50 --e2e-timeout=40
