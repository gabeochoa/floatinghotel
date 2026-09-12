#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
bash tests/run_unit_tests.sh test_diff_tools
message_repo=$(mktemp -d /tmp/fh-large-message.XXXXXX)
git -C "$message_repo" init -q -b main
printf 'File beneath the full message\n' > "$message_repo/code.txt"
git -C "$message_repo" add code.txt
awk 'BEGIN { print "Large expanded message\n"; for(i=1;i<=10000;i++) print "Message line " i; print "FULL_MESSAGE_END" }' |
  git -C "$message_repo" -c user.name=Test -c user.email=test@example.invalid commit -q -F -
output/floatinghotel.exe "$message_repo" --test-mode --headless \
  --test-script=tests/review_50/item_14.e2e \
  --screenshot-dir=output/screenshots/review-50 --e2e-timeout=60
bash tests/check_commit_message.sh
