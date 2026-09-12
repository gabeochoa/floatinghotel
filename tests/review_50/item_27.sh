#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture=$(mktemp -d /tmp/fh-item27.XXXXXX)
trap 'rm -rf "$fixture"' EXIT
git -C "$fixture" init -q -b main
git -C "$fixture" config user.name Reviewer
git -C "$fixture" config user.email review@example.invalid
printf 'first original\n' > "$fixture/a.cpp"
printf 'second original\n' > "$fixture/b.cpp"
git -C "$fixture" add .
git -C "$fixture" commit -qm Base
printf 'first replacement\n' > "$fixture/a.cpp"
printf 'second replacement\n' > "$fixture/b.cpp"
bash tests/run_unit_tests.sh test_review_store
output/floatinghotel.exe "$fixture" --test-mode --headless --test-script=tests/review_50/item_27.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
