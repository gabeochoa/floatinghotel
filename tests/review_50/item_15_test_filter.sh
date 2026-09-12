#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if nice -n 10 bash tests/run_unit_tests.sh definitely_not_a_test; then
    printf 'Unknown test suite must fail\n' >&2
    exit 1
fi
printf 'Unknown test suite rejected\n'
FH_SKIP_UNIT_TESTS=1 nice -n 10 bash tests/run_unit_tests.sh test_grep_capture
if FH_SKIP_UNIT_TESTS=1 nice -n 10 bash tests/run_unit_tests.sh definitely_not_a_test; then
    printf 'Skipping known suites must not accept unknown names\n' >&2
    exit 1
fi
