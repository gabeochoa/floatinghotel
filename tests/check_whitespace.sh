#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
space_repo=$(mktemp -d /tmp/floatinghotel-whitespace.XXXXXX)
trap 'rm -rf "$space_repo"' EXIT
git -C "$space_repo" init -q
git -C "$space_repo" config user.name 'UI test'
git -C "$space_repo" config user.email 'ui-test@example.invalid'
printf 'spacing only\n' > "$space_repo/spacing.txt"
seq 1 25 >> "$space_repo/spacing.txt"
printf 'semantic before\n' >> "$space_repo/spacing.txt"
git -C "$space_repo" add .
git -C "$space_repo" commit -qm baseline
printf 'spacing  only\n' > "$space_repo/spacing.txt"
seq 1 25 >> "$space_repo/spacing.txt"
printf 'semantic after\n' >> "$space_repo/spacing.txt"
output/floatinghotel.exe "$space_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_15_whitespace.e2e \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
