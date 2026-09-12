#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture=$(mktemp -d /tmp/fh-item21.XXXXXX)
trap 'rm -rf "$fixture"' EXIT
git -C "$fixture" init -q -b main
git -C "$fixture" config user.name Reviewer
git -C "$fixture" config user.email review@example.invalid
git -C "$fixture" commit --allow-empty -qm Base
printf 'small change\n' > "$fixture/a-small.cpp"
printf 'one\ntwo\nthree\nfour\nfive\n' > "$fixture/z-large.cpp"
git -C "$fixture" add .
git -C "$fixture" commit -qm SortSet
printf 'pending\n' >> "$fixture/a-small.cpp"
printf 'pending one\npending two\n' >> "$fixture/z-large.cpp"
bash tests/run_unit_tests.sh test_diff_tools
output/floatinghotel.exe "$fixture" --test-mode --headless --test-script=tests/review_50/item_21.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
