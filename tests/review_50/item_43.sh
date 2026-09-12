#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
repo_dir=$(mktemp -d /tmp/fh-review-43.XXXXXX)
trap 'rm -rf "$repo_dir"' EXIT
git -C "$repo_dir" init -q -b main
git -C "$repo_dir" config user.name 'Review 50'
git -C "$repo_dir" config user.email review50@example.invalid
printf 'base\n' > "$repo_dir/binary.dat"
printf 'base\n' > "$repo_dir/large.bin"
git -C "$repo_dir" add binary.dat large.bin
git -C "$repo_dir" commit -qm Baseline
printf '\x00\x01\x02\x03ABCD\x80\x81\x82\x83WXYZabcdefghijklmnop' > "$repo_dir/binary.dat"
for ((i=0; i<512; i++)); do printf '\0abcdefghijklmno'; done > "$repo_dir/large.bin"
bash tests/run_unit_tests.sh test_hex_view
output/floatinghotel.exe "$repo_dir" --test-mode --headless \
  --test-script=tests/review_50/item_43.e2e \
  --screenshot-dir=output/screenshots/review-50 --e2e-timeout=40
