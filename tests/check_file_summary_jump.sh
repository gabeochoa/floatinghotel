#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
jump_repo=$(mktemp -d /tmp/floatinghotel-jump.XXXXXX)
trap 'rm -rf "$jump_repo"' EXIT
git -C "$jump_repo" init -q
git -C "$jump_repo" config user.name 'UI test'
git -C "$jump_repo" config user.email 'ui-test@example.invalid'
awk 'BEGIN { for(i=1;i<=500;i++) print "large file line " i }' > "$jump_repo/first.txt"
printf 'Jump target first line\nJump target second line\n' > "$jump_repo/last.txt"
git -C "$jump_repo" add .
git -C "$jump_repo" commit -qm 'Two files with a distant second diff'
output/floatinghotel.exe "$jump_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_30_file_jump.e2e \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
