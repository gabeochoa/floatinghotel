#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
bash tests/run_unit_tests.sh test_content_reader
content_repo=$(mktemp -d /tmp/fh-async-file.XXXXXX)
git -C "$content_repo" init -q -b main
printf 'historical file content\n' > "$content_repo/source.cpp"
git -C "$content_repo" add source.cpp
git -C "$content_repo" -c user.name=Test -c user.email=test@example.invalid commit -qm 'Historical source'
printf 'working file content\n' > "$content_repo/source.cpp"
content_tools=$(mktemp -d /tmp/fh-async-file-tools.XXXXXX)
cp tests/helpers/slow_blob_git.sh "$content_tools/git"
chmod +x "$content_tools/git"
FH_REAL_GIT=$(command -v git) PATH="$content_tools:$PATH" \
  output/floatinghotel.exe "$content_repo" --test-mode --headless \
  --test-script=tests/review_50/item_01.e2e \
  --screenshot-dir=output/screenshots/review-50 --e2e-timeout=40
