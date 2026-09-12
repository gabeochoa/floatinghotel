#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
bash tests/run_unit_tests.sh test_byte_cache
bash tests/run_unit_tests.sh test_token_cache
syntax_repo=$(mktemp -d /tmp/fh-syntax-cache.XXXXXX)
git -C "$syntax_repo" init -q -b main
printf 'int answer = 41;\n' > "$syntax_repo/source.cpp"
git -C "$syntax_repo" add source.cpp
git -C "$syntax_repo" -c user.name=Test -c user.email=test@example.invalid commit -qm baseline
printf 'int answer = 42;\n' > "$syntax_repo/source.cpp"
output/floatinghotel.exe "$syntax_repo" --test-mode --headless \
  --test-script=tests/review_50/item_06.e2e \
  --screenshot-dir=output/screenshots/review-50 --e2e-timeout=40
