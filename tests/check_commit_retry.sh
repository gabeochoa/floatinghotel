#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
retry_repo=$(mktemp -d /tmp/floatinghotel-retry.XXXXXX)
trap 'rm -rf "$retry_repo"' EXIT
git -C "$retry_repo" init -q
git -C "$retry_repo" config user.name 'UI test'
git -C "$retry_repo" config user.email 'ui-test@example.invalid'
printf 'before\n' > "$retry_repo/file.txt"
printf 'file.txt diff=fail-once\n' > "$retry_repo/.gitattributes"
git -C "$retry_repo" add .
git -C "$retry_repo" commit -qm baseline
printf 'after retry\n' > "$retry_repo/file.txt"
git -C "$retry_repo" commit -qam 'Retry fixture'
git -C "$retry_repo" commit --allow-empty -qm 'Empty fixture'
git -C "$retry_repo" config diff.fail-once.textconv "bash $(pwd)/tests/helpers/fail_once_textconv.sh"
export FLOATINGHOTEL_TEST_MARKER="$retry_repo/converter-ran"
output/floatinghotel.exe "$retry_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_10_retry.e2e \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
