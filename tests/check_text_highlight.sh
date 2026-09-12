#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
highlight_repo=$(mktemp -d /tmp/fh-text-highlight.XXXXXX)
trap 'rm -rf "$highlight_repo"' EXIT
git -C "$highlight_repo" init -q -b main
git -C "$highlight_repo" config user.name 'Highlight test'
git -C "$highlight_repo" config user.email highlight@example.invalid
printf '0123XX6789 abcdefghijklmnopqrstuvwxyz\n' > "$highlight_repo/sample.txt"
for line in {1..80}; do printf 'row%02d abcdefghijklmnopqrstuvwxyz\n' "$line"; done >> "$highlight_repo/sample.txt"
git -C "$highlight_repo" add sample.txt
git -C "$highlight_repo" commit -qm 'Highlight fixture'
printf '0123456789 abcdefghijklmnopqrstuvwxyz\nsecond line for selection\n' > "$highlight_repo/sample.txt"
for line in {1..80}; do printf 'row%02d abcdefghijklmnopqrstuvwxyz\n' "$line"; done >> "$highlight_repo/sample.txt"
capture_dir=${1:-output/text-highlight}
mkdir -p "$capture_dir"
nice -n 10 output/floatinghotel.exe "$highlight_repo" --test-mode --headless \
    --test-script=tests/review_focus/text_highlight.e2e \
    --screenshot-dir="$capture_dir" --e2e-timeout=60 > "$capture_dir/native.log" 2>&1
