#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
audit_repo=$(mktemp -d /tmp/floatinghotel-navigation.XXXXXX)
trap 'rm -rf "$audit_repo"' EXIT
git -C "$audit_repo" init -q
git -C "$audit_repo" config user.name 'UI test'
git -C "$audit_repo" config user.email 'ui-test@example.invalid'
seq 1 1000 > "$audit_repo/lines.txt"
git -C "$audit_repo" add lines.txt
git -C "$audit_repo" commit -qm baseline
awk 'NR % 100 == 1 { print "changed " NR; next } { print }' "$audit_repo/lines.txt" > "$audit_repo/updated.txt"
mv "$audit_repo/updated.txt" "$audit_repo/lines.txt"
output/floatinghotel.exe "$audit_repo" --test-mode --headless \
  --test-script="${1:-tests/navigation_scripts/improvement_04_hunk_navigation.e2e}" \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
