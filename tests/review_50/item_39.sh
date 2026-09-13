#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
search_repo=$(mktemp -d /tmp/fh-review-search-globs.XXXXXX)
trap 'rm -rf "$search_repo"' EXIT
git -C "$search_repo" init -q -b main
git -C "$search_repo" config user.name 'Review test'
git -C "$search_repo" config user.email 'review@example.invalid'
mkdir -p "$search_repo/src"
printf 'cpp needle\n' > "$search_repo/src/app.cpp"
printf 'text needle\n' > "$search_repo/notes.txt"
git -C "$search_repo" add .
git -C "$search_repo" commit -qm baseline
output/floatinghotel.exe "$search_repo" --test-mode --headless --test-script=tests/review_50/item_39.e2e --screenshot-dir="${FH_SCREENSHOT_DIR:-output/screenshots/review_50}" --e2e-timeout=40
