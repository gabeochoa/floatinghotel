#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture_dir=$(mktemp -d /tmp/fh-review-search-caps.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
git -C "$fixture_dir" init -q -b main
git -C "$fixture_dir" config user.name 'Review 50'
git -C "$fixture_dir" config user.email review50@example.invalid
awk 'BEGIN { for (i=0;i<6000;i++) print "manyneedle" }' > "$fixture_dir/matches.txt"
awk 'BEGIN { printf "hugeneedle"; for (i=0;i<5*1024*1024;i++) printf "x"; print "" }' > "$fixture_dir/large.txt"
git -C "$fixture_dir" add .
git -C "$fixture_dir" commit -qm baseline
output/floatinghotel.exe "$fixture_dir" --test-mode --headless \
    --test-script=tests/review_50/item_15_search_caps.e2e \
    --screenshot-dir=output/screenshots/review-50 --e2e-timeout=60
