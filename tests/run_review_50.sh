#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
for ((number=1; number<=50; ++number)); do
    printf -v item '%02d' "$number"
    if [ ! -f "tests/review_50/item_$item.sh" ]; then
        printf 'Missing native verification script for item %s\n' "$item" >&2
        exit 2
    fi
done
mkdir -p output/review-50
run_dir=$(mktemp -d "$PWD/output/review-50/run.XXXXXX")
printf 'Verification logs: %s\n' "$run_dir"
nice -n 10 git rev-parse HEAD > "$run_dir/revision.log"
nice -n 10 git status --short > "$run_dir/worktree.log"
check() {
    local name="$1"
    shift
    printf 'RUN %s\n' "$name"
    printf '%q ' nice -n 10 "$@" > "$run_dir/$name.command"
    printf '\n' >> "$run_dir/$name.command"
    if nice -n 10 "$@" > "$run_dir/$name.log" 2>&1; then
        printf 'PASS %s\n' "$name" | tee -a "$run_dir/results.log"
    else
        printf 'FAIL %s\n' "$name" | tee -a "$run_dir/results.log"
        tail -60 "$run_dir/$name.log"
        return 1
    fi
}
if ! check build make -j1 OPT=-O2; then exit 1; fi
failed=0
check units env FH_SKIP_UNIT_TESTS=0 bash tests/run_unit_tests.sh || failed=1
for ((number=1; number<=50; ++number)); do
    printf -v item '%02d' "$number"
    check "item-$item" env FH_SKIP_UNIT_TESTS=1 FH_SKIP_BUILD=1 \
        bash "tests/review_50/item_$item.sh" || failed=1
done
check whitespace git diff --check || failed=1
printf 'Verification logs: %s\n' "$run_dir"
exit "$failed"
