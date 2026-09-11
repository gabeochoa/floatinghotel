#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
virtual_repo=$(mktemp -d /tmp/floatinghotel-virtual-commit.XXXXXX)
trap 'rm -rf "$virtual_repo"' EXIT
git -C "$virtual_repo" init -q -b main
git -C "$virtual_repo" config user.name 'UI test'
git -C "$virtual_repo" config user.email 'ui-test@example.invalid'
awk 'BEGIN { for(i=1;i<=20000;i++) print "large commit line " i }' > "$virtual_repo/first.txt"
printf 'Last file is reachable\n' > "$virtual_repo/last.txt"
git -C "$virtual_repo" add .
git -C "$virtual_repo" commit -qm 'Large virtualized commit'
output/floatinghotel.exe "$virtual_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_42_virtual_commit.e2e \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
