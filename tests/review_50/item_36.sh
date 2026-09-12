#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
search_repo=$(mktemp -d /tmp/fh-review-search-revision.XXXXXX)
trap 'rm -rf "$search_repo"' EXIT
git -C "$search_repo" init -q -b main
git -C "$search_repo" config user.name 'Review test'
git -C "$search_repo" config user.email 'review@example.invalid'
printf 'historical needle\n' > "$search_repo/odd:name.cpp"
git -C "$search_repo" add .
git -C "$search_repo" commit -qm baseline
printf 'current content\n' > "$search_repo/odd:name.cpp"
git -C "$search_repo" commit -qam later
printf 'working needle\n' > "$search_repo/odd:name.cpp"
output/floatinghotel.exe "$search_repo" --test-mode --headless --test-script=tests/review_50/item_36.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
