#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
tooltip_repo=$(mktemp -d /tmp/floatinghotel-tooltips.XXXXXX)
trap 'rm -rf "$tooltip_repo"' EXIT
git -C "$tooltip_repo" init -q -b feature/a-long-branch-name-that-must-remain-readable-in-a-tooltip
git -C "$tooltip_repo" config user.name 'UI test'
git -C "$tooltip_repo" config user.email 'ui-test@example.invalid'
mkdir -p "$tooltip_repo/src/a-very-long-directory-name/nested-path-for-tooltip-verification"
printf 'before\n' > "$tooltip_repo/src/a-very-long-directory-name/nested-path-for-tooltip-verification/long-file-name.txt"
git -C "$tooltip_repo" add .
git -C "$tooltip_repo" commit -qm 'A long commit subject that does not fit in the sidebar but should be fully readable when hovered, including the final words VISIBLE_END'
printf 'after\n' >> "$tooltip_repo/src/a-very-long-directory-name/nested-path-for-tooltip-verification/long-file-name.txt"
output/floatinghotel.exe "$tooltip_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_47_tooltips.e2e \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
