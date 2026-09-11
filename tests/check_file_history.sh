#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
history_repo=$(mktemp -d /tmp/floatinghotel-history.XXXXXX)
trap 'rm -rf "$history_repo"' EXIT
git -C "$history_repo" init -q
git -C "$history_repo" config user.name 'UI test'
git -C "$history_repo" config user.email 'ui-test@example.invalid'
printf 'first line\nsecond line\nthird line\n' > "$history_repo/original.txt"
git -C "$history_repo" add .
git -C "$history_repo" commit -qm 'Original file before rename'
git -C "$history_repo" mv original.txt renamed.txt
git -C "$history_repo" commit -qm 'Rename the file'
printf 'fourth line\n' >> "$history_repo/renamed.txt"
git -C "$history_repo" commit -qam 'Extend renamed file'
output/floatinghotel.exe "$history_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_25_file_history.e2e \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
