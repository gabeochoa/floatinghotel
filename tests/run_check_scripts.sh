#!/usr/bin/env bash
# Run the tests/check_*.sh scripts and report a summary.
# Usage: ./tests/run_check_scripts.sh [--all] [name ...]
#   With no names, runs every headless check. Native-window checks
#   (check_native_*, check_startup_actual) are skipped unless --all is given.
#   With names, runs only those (basename, with or without .sh).
# Requires output/floatinghotel.exe for the checks that launch the app.
set -uo pipefail
cd "$(dirname "$0")/.."

ALL=0
FILTER=()
for arg in "$@"; do
    case "$arg" in
        --all) ALL=1 ;;
        *) FILTER+=("$(basename "$arg" .sh)") ;;
    esac
done

is_native() {
    case "$1" in
        check_native_*|check_startup_actual) return 0 ;;
        *) return 1 ;;
    esac
}

PASSED=0 FAILED=0 SKIPPED=0
FAILED_NAMES=()
for script in tests/check_*.sh; do
    name=$(basename "$script" .sh)
    if [ "${#FILTER[@]}" -gt 0 ]; then
        keep=0
        for wanted in "${FILTER[@]}"; do [ "$wanted" = "$name" ] && keep=1; done
        [ "$keep" -eq 0 ] && continue
    elif [ "$ALL" -eq 0 ] && is_native "$name"; then
        printf 'SKIP %s (native window; pass --all to run)\n' "$name"
        SKIPPED=$((SKIPPED + 1))
        continue
    fi
    printf '=== %s ===\n' "$name"
    if bash "$script"; then
        PASSED=$((PASSED + 1))
    else
        printf 'FAIL %s\n' "$name"
        FAILED=$((FAILED + 1))
        FAILED_NAMES+=("$name")
    fi
done

echo "========================================"
echo "Checks: $PASSED passed, $FAILED failed, $SKIPPED skipped"
if [ "$FAILED" -gt 0 ]; then printf 'Failed: %s\n' "${FAILED_NAMES[*]}"; fi
[ "$FAILED" -eq 0 ]
