#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
link_repo=$(mktemp -d /tmp/fh-review-link.XXXXXX)
trap 'rm -rf "$link_repo"' EXIT
git -C "$link_repo" init -q -b main
git -C "$link_repo" config user.name 'Review test'
git -C "$link_repo" config user.email 'review@example.invalid'
ln -s missing-target "$link_repo/shortcut"
git -C "$link_repo" add .
git -C "$link_repo" commit -qm baseline
ln -snf target.txt "$link_repo/shortcut"
printf 'PRIVATE_TARGET_CONTENT\n' > "$link_repo/target.txt"
output/floatinghotel.exe "$link_repo" --test-mode --headless --test-script=tests/review_50/item_32.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
