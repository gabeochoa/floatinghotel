#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
search_repo=$(mktemp -d /tmp/fh-review-search-changed.XXXXXX)
trap 'rm -rf "$search_repo"' EXIT
git -C "$search_repo" init -q -b main
git -C "$search_repo" config user.name 'Review test'
git -C "$search_repo" config user.email 'review@example.invalid'
printf 'old contents\n' > "$search_repo/changed.txt"
printf 'unchanged needle\n' > "$search_repo/stable.txt"
printf 'deleted needle\n' > "$search_repo/removed.txt"
git -C "$search_repo" add .
git -C "$search_repo" commit -qm baseline
printf 'changed needle\n' > "$search_repo/changed.txt"
rm "$search_repo/removed.txt"
output/floatinghotel.exe "$search_repo" --test-mode --headless --test-script=tests/review_50/item_37.e2e --screenshot-dir="${FH_SCREENSHOT_DIR:-output/screenshots/review_50}" --e2e-timeout=40
