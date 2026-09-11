#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
visible_repo=$(mktemp -d /tmp/floatinghotel-visible-whitespace.XXXXXX)
trap 'rm -rf "$visible_repo"' EXIT
git -C "$visible_repo" init -q
git -C "$visible_repo" config user.name 'UI test'
git -C "$visible_repo" config user.email 'ui-test@example.invalid'
printf 'old\n' > "$visible_repo/visible.txt"
git -C "$visible_repo" add .
git -C "$visible_repo" commit -qm baseline
printf '\tvalue  \r\nlast line' > "$visible_repo/visible.txt"
output/floatinghotel.exe "$visible_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_16_visible_whitespace.e2e \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
git -C "$visible_repo" diff --exit-code
git -C "$visible_repo" show :visible.txt | cmp - "$visible_repo/visible.txt"
