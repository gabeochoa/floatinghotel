#!/bin/bash
# Build stress repos of increasing size and run the stress E2E scripts against
# each, printing only the bench lines.
#   tests/run_stress.sh [SIZE ...]      (default sizes: 100 1000 5000)
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SIZES=("$@")
[ ${#SIZES[@]} -eq 0 ] && SIZES=(100 1000 5000)

for n in "${SIZES[@]}"; do
    repo="/tmp/fh_stress_$n"
    "$PROJECT_DIR/scripts/make_stress_repo.sh" "$repo" \
        --tracked "$n" --modified $((n / 2)) --staged $((n / 20 + 1)) \
        --untracked $((n / 2)) --commits 100 > /dev/null
    echo "=== $n tracked files: $((n / 2)) modified, $((n / 20 + 1)) staged, $((n / 2)) untracked"
    "$SCRIPT_DIR/run_e2e.sh" -r "$repo" -d "$SCRIPT_DIR/stress_scripts" -t 120 2>&1 \
        | grep -aE "bench_frames|ms/frame|\[FAIL\]|E2E ERROR|Scripts passed" \
        | sed 's/^\[INFO\] //'
done
