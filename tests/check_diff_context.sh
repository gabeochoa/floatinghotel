#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
context_repo=$(mktemp -d /tmp/floatinghotel-context.XXXXXX)
trap 'rm -rf "$context_repo"' EXIT
git -C "$context_repo" init -q
git -C "$context_repo" config user.name 'UI test'
git -C "$context_repo" config user.email 'ui-test@example.invalid'
awk 'BEGIN { for (i=1;i<=100;i++) print "context line " i }' > "$context_repo/context.txt"
git -C "$context_repo" add .
git -C "$context_repo" commit -qm baseline
awk 'NR==50 { print "changed line 50"; next } { print }' "$context_repo/context.txt" > "$context_repo/updated.txt"
mv "$context_repo/updated.txt" "$context_repo/context.txt"
output/floatinghotel.exe "$context_repo" --test-mode --headless \
  --test-script="${1:-tests/navigation_scripts/improvement_17_context.e2e}" \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
