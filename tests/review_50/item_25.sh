#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture=$(mktemp -d /tmp/fh-item25.XXXXXX)
trap 'rm -rf "$fixture"' EXIT
git -C "$fixture" init -q -b main
git -C "$fixture" config user.name Reviewer
git -C "$fixture" config user.email review@example.invalid
printf 'original\n' > "$fixture/sample.cpp"
git -C "$fixture" add .
git -C "$fixture" commit -qm Base
printf 'replacement\n' > "$fixture/sample.cpp"
bash tests/run_unit_tests.sh test_review_store
bash tests/run_unit_tests.sh test_git_parser
output/floatinghotel.exe "$fixture" --test-mode --headless --test-script=tests/review_50/item_25.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
test -z "$(git -C "$fixture" diff --cached --name-only)"
