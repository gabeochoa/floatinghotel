#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
reading_repo=$(mktemp -d /tmp/floatinghotel-reading.XXXXXX)
trap 'rm -rf "$reading_repo"' EXIT
git -C "$reading_repo" init -q -b main
git -C "$reading_repo" config user.name 'UI test'
git -C "$reading_repo" config user.email 'ui-test@example.invalid'
awk 'BEGIN { for(i=1;i<=1000;i++) print "committed line " i }' > "$reading_repo/first.txt"
git -C "$reading_repo" add .
git -C "$reading_repo" commit -qm 'Long commit'
printf 'short committed file\n' > "$reading_repo/second.txt"
git -C "$reading_repo" add .
git -C "$reading_repo" commit -qm 'Short commit'
awk 'BEGIN { for(i=1;i<=1000;i++) print "working line " i }' > "$reading_repo/first.txt"
printf 'short working file\n' > "$reading_repo/second.txt"
output/floatinghotel.exe "$reading_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_43_reading_position.e2e \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
