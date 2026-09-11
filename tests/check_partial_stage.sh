#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
partial_repo=$(mktemp -d /tmp/floatinghotel-partial-stage.XXXXXX)
trap 'rm -rf "$partial_repo"' EXIT
git -C "$partial_repo" init -q
git -C "$partial_repo" config user.name 'UI test'
git -C "$partial_repo" config user.email 'ui-test@example.invalid'
printf 'first\nsecond\nthird\n' > "$partial_repo/lines.txt"
git -C "$partial_repo" add .
git -C "$partial_repo" commit -qm baseline
printf 'first\nadded one\nsecond\nadded two\nthird\n' > "$partial_repo/lines.txt"
output/floatinghotel.exe "$partial_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_19_partial_stage.e2e \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
git -C "$partial_repo" show :lines.txt | diff - <(printf 'first\nadded one\nsecond\nthird\n')
git -C "$partial_repo" diff -- lines.txt | grep -F '+added two'
