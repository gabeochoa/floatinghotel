#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture=$(mktemp -d /tmp/fh-item16.XXXXXX)
trap 'rm -rf "$fixture"' EXIT
git -C "$fixture" init -q -b main
git -C "$fixture" config user.name Reviewer
git -C "$fixture" config user.email review@example.invalid
printf 'original line\n' > "$fixture/sample.txt"
git -C "$fixture" add sample.txt
git -C "$fixture" commit -qm Initial
printf 'changed line\n' > "$fixture/sample.txt"
output/floatinghotel.exe "$fixture" --test-mode --headless --test-script=tests/review_50/item_16.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
test -z "$(git -C "$fixture" diff --cached --name-only)"
