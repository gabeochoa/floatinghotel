#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
scope_repo=$(mktemp -d /tmp/floatinghotel-review-scope.XXXXXX)
trap 'rm -rf "$scope_repo"' EXIT
git -C "$scope_repo" init -q -b main
git -C "$scope_repo" config user.name 'UI test'
git -C "$scope_repo" config user.email 'ui-test@example.invalid'
printf 'original\n' > "$scope_repo/code.txt"
git -C "$scope_repo" add .
git -C "$scope_repo" commit -qm baseline
git -C "$scope_repo" branch other
printf 'modified\n' > "$scope_repo/code.txt"
output/floatinghotel.exe "$scope_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_38_review_scope.e2e \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
