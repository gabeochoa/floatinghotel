#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
async_repo=$(mktemp -d /tmp/floatinghotel-async-commit.XXXXXX)
trap 'rm -rf "$async_repo"' EXIT
git -C "$async_repo" init -q -b main
git -C "$async_repo" config user.name 'UI test'
git -C "$async_repo" config user.email 'ui-test@example.invalid'
printf 'before\n' > "$async_repo/slow.txt"
printf 'slow.txt diff=slow\n' > "$async_repo/.gitattributes"
git -C "$async_repo" add .
git -C "$async_repo" commit -qm baseline
printf 'slow patch content\n' > "$async_repo/slow.txt"
git -C "$async_repo" commit -qam 'Slow commit'
printf 'fast patch content\n' > "$async_repo/fast.txt"
git -C "$async_repo" add .
git -C "$async_repo" commit -qm 'Fast commit'
git -C "$async_repo" config diff.slow.textconv "bash $(pwd)/tests/helpers/slow_textconv.sh"
output/floatinghotel.exe "$async_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_41_async_commit.e2e \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
