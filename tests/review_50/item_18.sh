#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture=$(mktemp -d /tmp/fh-item18.XXXXXX)
trap 'rm -rf "$fixture"' EXIT
git -C "$fixture" init -q -b main
git -C "$fixture" config user.name Reviewer
git -C "$fixture" config user.email review@example.invalid
printf 'neighbor before\noriginal line\nneighbor after\n' > "$fixture/sample.txt"
git -C "$fixture" add sample.txt
git -C "$fixture" commit -qm Initial
printf 'neighbor before\nreplacement line\nneighbor after\n' > "$fixture/sample.txt"
output/floatinghotel.exe "$fixture" --test-mode --headless --test-script=tests/review_50/item_18.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
