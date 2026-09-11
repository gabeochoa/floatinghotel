#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
compare_repo=$(mktemp -d /tmp/floatinghotel-compare.XXXXXX)
trap 'rm -rf "$compare_repo"' EXIT
git -C "$compare_repo" init -q -b base
git -C "$compare_repo" config user.name 'UI test'
git -C "$compare_repo" config user.email 'ui-test@example.invalid'
printf 'common content\n' > "$compare_repo/common.txt"
git -C "$compare_repo" add .
git -C "$compare_repo" commit -qm 'Common ancestor'
git -C "$compare_repo" checkout -qb target
printf 'target content\n' > "$compare_repo/target-only.txt"
git -C "$compare_repo" add .
git -C "$compare_repo" commit -qm 'Target work'
git -C "$compare_repo" checkout -q base
printf 'base content\n' > "$compare_repo/base-only.txt"
git -C "$compare_repo" add .
git -C "$compare_repo" commit -qm 'Base work'
output/floatinghotel.exe "$compare_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_28_compare.e2e \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
