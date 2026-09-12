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

    TOTAL=$((TOTAL + 1))
    local exe="$OUT_DIR/$name"

    echo "--- Compiling $name ---"
    if $CXX $CXXFLAGS $INCLUDES "$src" "$@" -o "$exe" 2>&1; then
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

if [ -z "$FILTER" ] || [ "$FILTER" = "test_review_target" ]; then
    run_test "test_review_target" "tests/unit/test_review_target.cpp"
fi

if [ -z "$FILTER" ] || [ "$FILTER" = "test_code_gutter" ]; then
    run_test "test_code_gutter" "tests/unit/test_code_gutter.cpp"
fi

if [ -z "$FILTER" ] || [ "$FILTER" = "test_diff_metrics" ]; then
    run_test "test_diff_metrics" "tests/unit/test_diff_metrics.cpp"
fi

if [ -z "$FILTER" ] || [ "$FILTER" = "test_byte_cache" ]; then
    run_test "test_byte_cache" "tests/unit/test_byte_cache.cpp"
fi

if [ -z "$FILTER" ] || [ "$FILTER" = "test_token_cache" ]; then
    run_test "test_token_cache" "tests/unit/test_token_cache.cpp"
fi

if [ -z "$FILTER" ] || [ "$FILTER" = "test_repository_lock" ]; then
    run_test "test_repository_lock" "tests/unit/test_repository_lock.cpp" \
        "src/git/git_runner.cpp" "src/util/process.cpp"
fi

if [ -z "$FILTER" ] || [ "$FILTER" = "test_async_task" ]; then
    run_test "test_async_task" "tests/unit/test_async_task.cpp"
fi

if [ -z "$FILTER" ] || [ "$FILTER" = "test_content_reader" ]; then
    run_test "test_content_reader" "tests/unit/test_content_reader.cpp" \
        "src/git/content_reader.cpp" "src/git/git_runner.cpp" "src/util/process.cpp" \
        "vendor/afterhours/src/plugins/files.cpp"
fi

if [ -z "$FILTER" ] || [ "$FILTER" = "test_repository_search" ]; then
    run_test "test_repository_search" "tests/unit/test_repository_search.cpp" \
        "src/git/repository_search.cpp" "src/git/git_parser.cpp" \
        "src/git/git_runner.cpp" "src/util/process.cpp" \
        "vendor/afterhours/src/plugins/files.cpp"
fi

if [ -z "$FILTER" ] || [ "$FILTER" = "test_review_snapshot" ]; then
    run_test "test_review_snapshot" "tests/unit/test_review_snapshot.cpp" \
        "src/review_snapshot.cpp" "src/git/git_runner.cpp" "src/util/process.cpp" \
        "vendor/afterhours/src/plugins/files.cpp"
fi

if [ -z "$FILTER" ] || [ "$FILTER" = "test_diff_tools" ]; then
    run_test "test_diff_tools" "tests/unit/test_diff_tools.cpp"
fi

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

if [ -z "$FILTER" ] || [ "$FILTER" = "test_text_decode" ]; then
    run_test "test_text_decode" \
        "tests/unit/test_text_decode.cpp"
fi

if [ -z "$FILTER" ] || [ "$FILTER" = "test_hex_view" ]; then
    run_test "test_hex_view" \
        "tests/unit/test_hex_view.cpp"
fi

if [ -z "$FILTER" ] || [ "$FILTER" = "test_image_view_state" ]; then
    run_test "test_image_view_state" \
        "tests/unit/test_image_view_state.cpp"
fi

if [ -z "$FILTER" ] || [ "$FILTER" = "test_markdown_preview" ]; then
    run_test "test_markdown_preview" \
        "tests/unit/test_markdown_preview.cpp"
fi

# --- test_review_store ---
if [ -z "$FILTER" ] || [ "$FILTER" = "test_review_store" ]; then
    run_test "test_review_store" \
        "tests/unit/test_review_store.cpp" \
        "src/review_store.cpp" \
        "vendor/afterhours/src/plugins/files.cpp"
fi

echo "========================================"
echo "Results: $PASSED/$TOTAL passed, $FAILED failed"
echo "========================================"

[ "$FAILED" -eq 0 ]
