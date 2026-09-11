#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
long_repo=$(mktemp -d /tmp/floatinghotel-long-lines.XXXXXX)
trap 'rm -rf "$long_repo"' EXIT
git -C "$long_repo" init -q
git -C "$long_repo" config user.name 'UI test'
git -C "$long_repo" config user.email 'ui-test@example.invalid'
printf 'baseline\n' > "$long_repo/long.txt"
git -C "$long_repo" add .
git -C "$long_repo" commit -qm baseline
printf 'START %0400d END_OF_LONG_LINE\n' 1 > "$long_repo/long.txt"
output/floatinghotel.exe "$long_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_12_long_lines.e2e \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
