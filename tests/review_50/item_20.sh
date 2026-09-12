#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture=$(mktemp -d /tmp/fh-item20.XXXXXX)
trap 'rm -rf "$fixture"' EXIT
git -C "$fixture" init -q -b main
git -C "$fixture" config user.name Reviewer
git -C "$fixture" config user.email review@example.invalid
printf 'original\n' > "$fixture/existing.cpp"
printf 'deleted-body\n' > "$fixture/gone.cpp"
git -C "$fixture" add .
git -C "$fixture" commit -qm Base
printf 'existing-body\n' > "$fixture/existing.cpp"
printf 'added-body\n' > "$fixture/added.cpp"
printf 'python-body\n' > "$fixture/script.py"
git -C "$fixture" rm -q gone.cpp
git -C "$fixture" add .
git -C "$fixture" commit -qm FilterSet
bash tests/run_unit_tests.sh test_review_files
output/floatinghotel.exe "$fixture" --test-mode --headless --test-script=tests/review_50/item_20.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
