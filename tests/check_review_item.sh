#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
item=${1:?Pass an item number from 01 to 50}
case "$item" in
  0[1-9]|[1-4][0-9]|50) ;;
  *) exit 2 ;;
esac
mkdir -p output/review-50 output/screenshots/review-50
make -j2 OPT="${OPT:--O0}" > "output/review-50/build-$item.log" 2>&1
if test -f "tests/review_50/item_$item.sh"; then
  bash "tests/review_50/item_$item.sh" > "output/review-50/item-$item.log" 2>&1
elif test -f "tests/review_50/item_$item.e2e"; then
  output/floatinghotel.exe "${FH_TEST_REPO:-/tmp/floatinghotel_test_repo}" \
    --test-mode --headless --test-script="tests/review_50/item_$item.e2e" \
    --screenshot-dir=output/screenshots/review-50 --e2e-timeout=60 \
    > "output/review-50/item-$item.log" 2>&1
else
  printf 'No verification scenario for item %s\n' "$item" >&2
  exit 2
fi
git diff --check
printf 'PASS item %s\n' "$item"
