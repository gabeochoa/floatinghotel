#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture=$(mktemp -d /tmp/fh-item28-empty.XXXXXX)
trap 'rm -rf "$fixture"' EXIT
git -C "$fixture" init -q -b main
git -C "$fixture" config user.name Reviewer
git -C "$fixture" config user.email review@example.invalid
printf 'base\n' > "$fixture/file.txt"
git -C "$fixture" add .
git -C "$fixture" commit -qm Base
git -C "$fixture" commit --allow-empty -qm EmptyStep
printf 'changed\n' > "$fixture/file.txt"
git -C "$fixture" commit -qam ChangedStep
output/floatinghotel.exe "$fixture" --test-mode --headless --test-script=tests/review_50/item_28_empty.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
