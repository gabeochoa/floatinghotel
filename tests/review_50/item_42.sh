#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
repo_dir=$(mktemp -d /tmp/fh-review-42.XXXXXX)
trap 'rm -rf "$repo_dir"' EXIT
git -C "$repo_dir" init -q -b main
git -C "$repo_dir" config user.name 'Review 50'
git -C "$repo_dir" config user.email review50@example.invalid
printf 'base\n' > "$repo_dir/utf16.txt"
git -C "$repo_dir" add utf16.txt
git -C "$repo_dir" commit -qm Baseline
printf '\xff\xfe\x68\x00\x65\x00\x6c\x00\x6c\x00\x6f\x00\x20\x00\x75\x00\x74\x00\x66\x00\x31\x00\x36\x00\x0a\x00' > "$repo_dir/utf16.txt"
output/floatinghotel.exe "$repo_dir" --test-mode --headless \
  --test-script=tests/review_50/item_42.e2e \
  --screenshot-dir=output/screenshots/review-50 --e2e-timeout=40
