#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
patch_repo=$(mktemp -d /tmp/fh-review-commit-worker.XXXXXX)
trap 'rm -rf "$patch_repo"' EXIT
git -C "$patch_repo" init -q -b main
git -C "$patch_repo" config user.name 'Review test'
git -C "$patch_repo" config user.email 'review@example.invalid'
printf 'root source\n' > "$patch_repo/app.cpp"
git -C "$patch_repo" add .
git -C "$patch_repo" commit -qm baseline
printf 'latest source\n' > "$patch_repo/app.cpp"
git -C "$patch_repo" commit -qam latest
perl -e 'print "x" x 4096, " END_OF_LONG_LINE\n"' > "$patch_repo/long.txt"
git -C "$patch_repo" add long.txt
git -C "$patch_repo" commit -qm 'Long line'
printf 'working source\n' > "$patch_repo/app.cpp"
output/floatinghotel.exe "$patch_repo" --test-mode --headless --test-script=tests/review_50/item_11.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40 | tee "$patch_repo/.git/native.log"
awk -F 'scroll_x="' '/name="(commit_detail_scroll|diff_scroll)"/ { split($2, offset, "\""); values[++count] = offset[1] }
    END { exit !(count == 4 && values[1] == 0 && values[2] > 0 && values[3] == 0 && values[4] == 0) }' "$patch_repo/.git/native.log"
