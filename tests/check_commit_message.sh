#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
message_repo=$(mktemp -d /tmp/floatinghotel-message.XXXXXX)
trap 'rm -rf "$message_repo"' EXIT
git -C "$message_repo" init -q -b main
git -C "$message_repo" config user.name 'UI test'
git -C "$message_repo" config user.email 'ui-test@example.invalid'
printf 'File beneath the full message\n' > "$message_repo/code.txt"
git -C "$message_repo" add code.txt
awk 'BEGIN { print "Long commit message\n"; for(i=1;i<=80;i++) print "Message line " i; print "FULL_MESSAGE_END" }' | git -C "$message_repo" commit -q -F -
output/floatinghotel.exe "$message_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_49_full_message.e2e \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
