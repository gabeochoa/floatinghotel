#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture=$(mktemp -d /tmp/fh-item17.XXXXXX)
trap 'rm -rf "$fixture"' EXIT
git -C "$fixture" init -q -b main
git -C "$fixture" config user.name Reviewer
git -C "$fixture" config user.email review@example.invalid
printf 'base version\n' > "$fixture/sample.txt"
git -C "$fixture" add sample.txt
git -C "$fixture" commit -qm Base
printf 'target version\n' > "$fixture/sample.txt"
git -C "$fixture" add sample.txt
git -C "$fixture" commit -qm Target
bash tests/run_unit_tests.sh test_review_store
output/floatinghotel.exe "$fixture" --test-mode --headless --test-script=tests/review_50/item_17.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
