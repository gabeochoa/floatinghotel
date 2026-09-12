#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture=$(mktemp -d /tmp/fh-item19.XXXXXX)
trap 'rm -rf "$fixture"' EXIT
git -C "$fixture" init -q -b main
git -C "$fixture" config user.name Reviewer
git -C "$fixture" config user.email review@example.invalid
git -C "$fixture" commit --allow-empty -qm Base
mkdir -p "$fixture/vendor" "$fixture/generated"
printf 'vendor-dependency\n' > "$fixture/vendor/dependency.txt"
printf 'generated-output\n' > "$fixture/generated/output.txt"
printf 'locked-dependency\n' > "$fixture/Cargo.lock"
printf 'application-code\n' > "$fixture/app.txt"
git -C "$fixture" add .
git -C "$fixture" commit -qm ChangeSet
printf 'pending\n' >> "$fixture/vendor/dependency.txt"
printf 'pending\n' >> "$fixture/generated/output.txt"
printf 'pending\n' >> "$fixture/Cargo.lock"
printf 'pending\n' >> "$fixture/app.txt"
bash tests/run_unit_tests.sh test_review_files
output/floatinghotel.exe "$fixture" --test-mode --headless --test-script=tests/review_50/item_19.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
