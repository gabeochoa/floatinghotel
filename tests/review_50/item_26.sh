#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
fixture=$(mktemp -d /tmp/fh-item26.XXXXXX)
shots=$(mktemp -d /tmp/fh-item26-shots.XXXXXX)
modifier=
trap 'if [ -n "$modifier" ]; then kill "$modifier" 2>/dev/null || true; fi; rm -rf "$fixture" "$shots"' EXIT
git -C "$fixture" init -q -b main
git -C "$fixture" config user.name Reviewer
git -C "$fixture" config user.email review@example.invalid
printf 'original\n' > "$fixture/sample.cpp"
git -C "$fixture" add .
git -C "$fixture" commit -qm Base
printf 'replacement\n' > "$fixture/sample.cpp"
bash tests/run_unit_tests.sh test_review_store
(
    for attempt in $(seq 1 300); do
        if [ -f "$shots/item_26_before.png" ]; then
            sleep 1
            printf 'changed again\n' > "$fixture/sample.cpp"
            exit 0
        fi
        sleep 0.1
    done
    exit 1
) &
modifier=$!
output/floatinghotel.exe "$fixture" --test-mode --headless --test-script=tests/review_50/item_26.e2e --screenshot-dir="$shots" --e2e-timeout=40
wait "$modifier"
modifier=
mkdir -p output/screenshots/review_50
cp "$shots/item_26_before.png" "$shots/item_26_outdated.png" output/screenshots/review_50/
