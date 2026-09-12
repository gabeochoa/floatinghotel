#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
move_repo=$(mktemp -d /tmp/fh-review-moved.XXXXXX)
trap 'rm -rf "$move_repo"' EXIT
git -C "$move_repo" init -q -b main
git -C "$move_repo" config user.name 'Review test'
git -C "$move_repo" config user.email 'review@example.invalid'
awk 'BEGIN {for(i=1;i<=4;i++) print "int moved_value_" i " = " i ";"; for(i=1;i<=20;i++) print "int stable_value_" i " = " i ";"}' > "$move_repo/move.cpp"
git -C "$move_repo" add .
git -C "$move_repo" commit -qm baseline
awk 'BEGIN {for(i=1;i<=20;i++) print "int stable_value_" i " = " i ";"; for(i=1;i<=4;i++) print "int moved_value_" i " = " i ";"}' > "$move_repo/move.cpp"
output/floatinghotel.exe "$move_repo" --test-mode --headless --test-script=tests/review_50/item_35.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
