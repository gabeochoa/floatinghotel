#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
cache_repo=$(mktemp -d /tmp/fh-review-commit-cache.XXXXXX)
trap 'rm -rf "$cache_repo"' EXIT
git -C "$cache_repo" init -q -b main
git -C "$cache_repo" config user.name 'Review test'
git -C "$cache_repo" config user.email 'review@example.invalid'
perl -e 'print $_ == 8 ? "faraway context\n" : "context line $_\n" for 1..60' > "$cache_repo/app.cpp"
printf 'int value = 1;\n' > "$cache_repo/spacing.cpp"
git -C "$cache_repo" add .
git -C "$cache_repo" commit -qm baseline
perl -e 'print $_ == 30 ? "focus changed\n" : $_ == 8 ? "faraway context\n" : "context line $_\n" for 1..60' > "$cache_repo/app.cpp"
printf 'int  value = 1;\n' > "$cache_repo/spacing.cpp"
git -C "$cache_repo" commit -qam latest
output/floatinghotel.exe "$cache_repo" --test-mode --headless --test-script=tests/review_50/item_03.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40 | tee "$cache_repo/.git/native.log"
grep -q 'commit patch cache hit' "$cache_repo/.git/native.log"
