#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
tab_repo=$(mktemp -d /tmp/fh-long-tab.XXXXXX)
git -C "$tab_repo" init -q -b review/a-long-branch-name-that-must-not-overlap-the-new-tab-button
git -C "$tab_repo" -c user.name=Test -c user.email=test@example.invalid commit -q --allow-empty -m Baseline
output/floatinghotel.exe "$tab_repo" --test-mode --headless \
  --test-script=tests/review_50/item_50.e2e \
  --screenshot-dir=output/screenshots/review-50 --e2e-timeout=40
