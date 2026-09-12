#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
repo_dir=$(mktemp -d /tmp/fh-review-13.XXXXXX)
trap 'rm -rf "$repo_dir"' EXIT
git -C "$repo_dir" init -q -b main
git -C "$repo_dir" config user.name 'Review 50'
git -C "$repo_dir" config user.email review50@example.invalid
for n in $(seq 1 120); do printf 'line %03d\n' "$n"; done > "$repo_dir/README.md"
git -C "$repo_dir" add README.md
git -C "$repo_dir" commit -qm Baseline
printf 'working change\n' >> "$repo_dir/README.md"
for n in $(seq 1 160); do printf 'added line %03d\n' "$n"; done >> "$repo_dir/README.md"
output/floatinghotel.exe "$repo_dir" --test-mode --headless \
  --test-script=tests/review_50/item_13.e2e \
  --screenshot-dir=output/screenshots/review-50 --e2e-timeout=40
