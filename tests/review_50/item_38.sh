#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
search_repo=$(mktemp -d /tmp/fh-review-search-matching.XXXXXX)
trap 'rm -rf "$search_repo"' EXIT
git -C "$search_repo" init -q -b main
git -C "$search_repo" config user.name 'Review test'
git -C "$search_repo" config user.email 'review@example.invalid'
printf 'needle\nneedle_suffix\nNEEDLE\ndifferent\n' > "$search_repo/code.txt"
git -C "$search_repo" add .
git -C "$search_repo" commit -qm baseline
output/floatinghotel.exe "$search_repo" --test-mode --headless --test-script=tests/review_50/item_38.e2e --screenshot-dir="${FH_SCREENSHOT_DIR:-output/screenshots/review_50}" --e2e-timeout=40
