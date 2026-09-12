#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mode_repo=$(mktemp -d /tmp/fh-review-mode.XXXXXX)
trap 'rm -rf "$mode_repo"' EXIT
git -C "$mode_repo" init -q -b main
git -C "$mode_repo" config user.name 'Review test'
git -C "$mode_repo" config user.email 'review@example.invalid'
printf 'echo hello\n' > "$mode_repo/run.sh"
git -C "$mode_repo" add .
git -C "$mode_repo" commit -qm baseline
chmod +x "$mode_repo/run.sh"
output/floatinghotel.exe "$mode_repo" --test-mode --headless --test-script=tests/review_50/item_31.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
