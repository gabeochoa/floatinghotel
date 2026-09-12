#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
menu_repo=$(mktemp -d /tmp/fh-menu-actions.XXXXXX)
git -C "$menu_repo" init -q -b main
printf 'before\n' > "$menu_repo/main.cpp"
git -C "$menu_repo" add main.cpp
git -C "$menu_repo" -c user.name=Test -c user.email=test@example.invalid commit -qm Baseline
printf 'after\n' > "$menu_repo/main.cpp"
output/floatinghotel.exe "$menu_repo" --test-mode --headless \
  --test-script=tests/review_50/item_49.e2e \
  --screenshot-dir=output/screenshots/review-50 --e2e-timeout=40
