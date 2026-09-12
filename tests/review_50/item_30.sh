#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture=$(mktemp -d /tmp/fh-item30.XXXXXX)
trap 'rm -rf "$fixture"' EXIT
git -C "$fixture" init -q -b main
git -C "$fixture" config user.name Reviewer
git -C "$fixture" config user.email review@example.invalid
printf 'old parent content\n' > "$fixture/deleted.txt"
git -C "$fixture" add .
git -C "$fixture" commit -qm Base
git -C "$fixture" branch side
git -C "$fixture" rm -q deleted.txt
git -C "$fixture" commit -qm LeftDeletes
git -C "$fixture" checkout -q side
printf 'side content\n' > "$fixture/side.txt"
git -C "$fixture" add .
git -C "$fixture" commit -qm SideAdds
git -C "$fixture" checkout -q main
git -C "$fixture" merge -q --no-ff side -m MergeSet
bash tests/run_unit_tests.sh test_diff_tools
output/floatinghotel.exe "$fixture" --test-mode --headless --test-script=tests/review_50/item_30.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
