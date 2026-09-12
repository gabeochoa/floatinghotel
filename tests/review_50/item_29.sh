#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture=$(mktemp -d /tmp/fh-item29.XXXXXX)
trap 'rm -rf "$fixture"' EXIT
git -C "$fixture" init -q -b main
git -C "$fixture" config user.name Reviewer
git -C "$fixture" config user.email review@example.invalid
printf 'original\n' > "$fixture/sample.cpp"
git -C "$fixture" add .
git -C "$fixture" commit -qm Base
git -C "$fixture" tag base
git -C "$fixture" checkout -qb old-series
printf 'common addition\nold implementation\n' >> "$fixture/sample.cpp"
git -C "$fixture" commit -qam OldSeries
git -C "$fixture" checkout -qb new-series base
printf 'common addition\nnew implementation\n' >> "$fixture/sample.cpp"
git -C "$fixture" commit -qam NewSeries
git -C "$fixture" range-diff --no-color --no-ext-diff --no-textconv base..old-series base..new-series
bash tests/run_unit_tests.sh test_diff_tools
output/floatinghotel.exe "$fixture" --test-mode --headless --test-script=tests/review_50/item_29.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
