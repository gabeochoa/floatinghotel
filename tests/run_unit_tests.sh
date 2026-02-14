#!/usr/bin/env bash
# Compile and run all unit tests.
# Usage: ./tests/run_unit_tests.sh [test_name]
#   If test_name is provided, only compile/run that test (e.g. "test_git_parser").
#   Otherwise, all tests are compiled and run.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CXX="${CXX:-clang++}"
CXXSTD="-std=c++23"
CXXFLAGS="$CXXSTD -g -O0 -Wall -Wextra -Wpedantic \
    -Wno-deprecated-volatile -Wno-missing-field-initializers \
    -Wno-sign-conversion -Wno-implicit-int-float-conversion"

INCLUDES="-isystem vendor/ -isystem vendor/afterhours/vendor/ -I."

OUT_DIR="output/tests"
mkdir -p "$OUT_DIR"

PASSED=0
FAILED=0
TOTAL=0

run_test() {
    local name="$1"
    local src="$2"
    shift 2
    local extra_srcs=("$@")

    TOTAL=$((TOTAL + 1))
    local exe="$OUT_DIR/$name"

    echo "--- Compiling $name ---"
    if $CXX $CXXFLAGS $INCLUDES "$src" "${extra_srcs[@]}" -o "$exe" 2>&1; then
        echo "--- Running $name ---"
        if "$exe"; then
            PASSED=$((PASSED + 1))
        else
            echo "FAILED: $name (runtime)"
            FAILED=$((FAILED + 1))
        fi
    else
        echo "FAILED: $name (compile)"
        FAILED=$((FAILED + 1))
    fi
    echo ""
}

FILTER="${1:-}"

# --- test_git_parser ---
if [ -z "$FILTER" ] || [ "$FILTER" = "test_git_parser" ]; then
    run_test "test_git_parser" \
        "tests/unit/test_git_parser.cpp" \
        "src/git/git_parser.cpp"
fi

# --- test_error_humanizer ---
if [ -z "$FILTER" ] || [ "$FILTER" = "test_error_humanizer" ]; then
    run_test "test_error_humanizer" \
        "tests/unit/test_error_humanizer.cpp" \
        "src/git/error_humanizer.cpp"
fi

# --- test_process ---
if [ -z "$FILTER" ] || [ "$FILTER" = "test_process" ]; then
    run_test "test_process" \
        "tests/unit/test_process.cpp" \
        "src/util/process.cpp"
fi

# --- test_settings ---
if [ -z "$FILTER" ] || [ "$FILTER" = "test_settings" ]; then
    run_test "test_settings" \
        "tests/unit/test_settings.cpp" \
        "src/settings.cpp" \
        "vendor/afterhours/src/plugins/files.cpp"
fi

echo "========================================"
echo "Results: $PASSED/$TOTAL passed, $FAILED failed"
echo "========================================"

[ "$FAILED" -eq 0 ]
