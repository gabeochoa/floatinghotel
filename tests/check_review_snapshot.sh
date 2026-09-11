#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
snapshot_repo=$(mktemp -d /tmp/floatinghotel-review-snapshot.XXXXXX)
trap 'rm -rf "$snapshot_repo"' EXIT
git -C "$snapshot_repo" init -q -b main
git -C "$snapshot_repo" config user.name 'UI test'
git -C "$snapshot_repo" config user.email 'ui-test@example.invalid'
printf 'original\n' > "$snapshot_repo/code.txt"
git -C "$snapshot_repo" add .
git -C "$snapshot_repo" commit -qm baseline
printf 'already reviewed\n' > "$snapshot_repo/code.txt"
output/floatinghotel.exe "$snapshot_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_39_review_snapshot.e2e \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
test -s "$snapshot_repo/.git/floatinghotel-baseline.cbor"
test -z "$(git -C "$snapshot_repo" diff --cached --name-only)"
