#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
owners_repo=$(mktemp -d /tmp/fh-review-owners.XXXXXX)
trap 'rm -rf "$owners_repo"' EXIT
git -C "$owners_repo" init -q -b main
git -C "$owners_repo" config user.name 'Review test'
git -C "$owners_repo" config user.email 'review@example.invalid'
mkdir -p "$owners_repo/.github"
printf '*.cpp @original-team\n' > "$owners_repo/.github/CODEOWNERS"
printf '* @fallback\n' > "$owners_repo/CODEOWNERS"
printf 'int old_value;\n' > "$owners_repo/code.cpp"
git -C "$owners_repo" add .
git -C "$owners_repo" commit -qm baseline
printf '*.cpp @current-team\n' > "$owners_repo/.github/CODEOWNERS"
printf 'int new_value;\n' > "$owners_repo/code.cpp"
output/floatinghotel.exe "$owners_repo" --test-mode --headless --test-script=tests/review_50/item_34.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
