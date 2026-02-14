#!/bin/bash
# Floatinghotel E2E Test Runner
# Uses afterhours E2E testing plugin with --test-mode and .e2e scripts.
# Takes screenshots during UI interactions for design audit.
#
# Usage: ./tests/run_e2e.sh [OPTIONS] [SCRIPT_FILTER]
#   -t, --timeout SEC   Set timeout per script (default: 30)
#   -d, --dir DIR       E2E scripts directory (default: tests/e2e_scripts)
#   SCRIPT_FILTER        Only run scripts matching this pattern

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="$PROJECT_DIR/output"
E2E_SCRIPTS_DIR="$SCRIPT_DIR/e2e_scripts"
SCREENSHOT_DIR="$OUTPUT_DIR/screenshots/e2e_audit"
EXECUTABLE="$OUTPUT_DIR/floatinghotel.exe"
REPO_PATH="$PROJECT_DIR/tests/fixture_repo"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Defaults
TIMEOUT=30
FILTER=""

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--timeout) TIMEOUT="$2"; shift 2 ;;
        -d|--dir) E2E_SCRIPTS_DIR="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS] [FILTER]"
            echo "  -t, --timeout SEC   Timeout per script (default: 30)"
            echo "  -d, --dir DIR       Scripts directory"
            echo "  FILTER              Script name filter"
            exit 0 ;;
        *) FILTER="$1"; shift ;;
    esac
done

echo "=============================================="
echo "   Floatinghotel E2E Test Runner"
echo "=============================================="
echo ""

# Create fixture repo if needed
if [ ! -d "$REPO_PATH/.git" ]; then
    bash "$SCRIPT_DIR/create_fixture_repo.sh"
fi

# Build if needed
if [ ! -f "$EXECUTABLE" ]; then
    echo -e "${YELLOW}Building application...${NC}"
    cd "$PROJECT_DIR" && make
fi

# Clean screenshots
rm -rf "$SCREENSHOT_DIR"
mkdir -p "$SCREENSHOT_DIR"

# Find scripts
SCRIPTS=()
for script in "$E2E_SCRIPTS_DIR"/*.e2e; do
    [ -f "$script" ] || continue
    name="$(basename "$script" .e2e)"
    if [ -n "$FILTER" ] && ! echo "$name" | grep -qi "$FILTER"; then
        continue
    fi
    SCRIPTS+=("$script")
done

if [ ${#SCRIPTS[@]} -eq 0 ]; then
    echo -e "${YELLOW}No E2E scripts found${NC}"
    exit 0
fi

echo "Found ${#SCRIPTS[@]} script(s)"
echo "Screenshots: $SCREENSHOT_DIR"
echo "Repo: $REPO_PATH"
echo ""

# Validation report directory
VALIDATION_DIR="$OUTPUT_DIR/validation"
mkdir -p "$VALIDATION_DIR"

# Run each script individually for isolation
PASSED=0
FAILED=0
ALL_VALIDATION_WARNINGS=""

for script in "${SCRIPTS[@]}"; do
    name="$(basename "$script" .e2e)"
    echo -e "${BLUE}--- $name ---${NC}"

    SCRIPT_SCREENSHOT_DIR="$SCREENSHOT_DIR/$name"
    SCRIPT_LOG="$VALIDATION_DIR/${name}.log"
    SCRIPT_REPORT="$VALIDATION_DIR/${name}.json"
    mkdir -p "$SCRIPT_SCREENSHOT_DIR"

    if "$EXECUTABLE" "$REPO_PATH" \
        --test-mode \
        --test-script="$script" \
        --screenshot-dir="$SCRIPT_SCREENSHOT_DIR" \
        --e2e-timeout="$TIMEOUT" \
        --validation-report="$SCRIPT_REPORT" \
        2>&1 | tee "$SCRIPT_LOG"; then
        echo -e "  ${GREEN}[PASS]${NC} $name"
        PASSED=$((PASSED + 1))
    else
        echo -e "  ${RED}[FAIL]${NC} $name (exit code: $?)"
        FAILED=$((FAILED + 1))
    fi

    # Count screenshots taken
    SC_COUNT=$(ls "$SCRIPT_SCREENSHOT_DIR"/*.png 2>/dev/null | wc -l | tr -d ' ')
    echo "  Screenshots: $SC_COUNT"

    # Extract validation warnings from log
    WARN_COUNT=$(grep -c "\[UI Validation\]" "$SCRIPT_LOG" 2>/dev/null || true)
    if [ "$WARN_COUNT" -gt 0 ]; then
        echo -e "  ${YELLOW}Validation warnings: $WARN_COUNT${NC}"
    else
        echo -e "  Validation warnings: 0"
    fi
    echo ""
done

# Summary
echo "=============================================="
echo "   E2E Test Summary"
echo "=============================================="
echo ""
echo "  Total:   $((PASSED + FAILED))"
echo -e "  Passed:  ${GREEN}$PASSED${NC}"
echo -e "  Failed:  ${RED}$FAILED${NC}"
echo ""
echo "Screenshots saved to: $SCREENSHOT_DIR"

# Count total screenshots
TOTAL_SC=$(find "$SCREENSHOT_DIR" -name "*.png" 2>/dev/null | wc -l | tr -d ' ')
echo "Total screenshots: $TOTAL_SC"

# Validation summary across all scripts
echo ""
echo "=============================================="
echo "   Validation Summary"
echo "=============================================="
echo ""
echo "Reports saved to: $VALIDATION_DIR/"

# Merge all validation reports
TOTAL_VIOLATIONS=0
for report in "$VALIDATION_DIR"/*.json; do
    [ -f "$report" ] || continue
    name="$(basename "$report" .json)"
    count=$(grep -c '"message"' "$report" 2>/dev/null || true)
    if [ "$count" -gt 0 ]; then
        echo -e "  ${YELLOW}$name: $count unique violations${NC}"
        TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + count))
    else
        echo -e "  ${GREEN}$name: clean${NC}"
    fi
done

if [ "$TOTAL_VIOLATIONS" -gt 0 ]; then
    echo ""
    echo -e "${YELLOW}Total unique violations across all scripts: $TOTAL_VIOLATIONS${NC}"
    echo "Review individual reports in $VALIDATION_DIR/ for details."

    # Print the deduplicated summary from the first log that has it
    for logfile in "$VALIDATION_DIR"/*.log; do
        [ -f "$logfile" ] || continue
        if grep -q "UI Validation Summary" "$logfile" 2>/dev/null; then
            echo ""
            echo "--- Validation Details (from $(basename "$logfile")) ---"
            sed -n '/=== UI Validation Summary/,/=== End Validation Summary ===/p' "$logfile"
            break
        fi
    done
else
    echo ""
    echo -e "${GREEN}No validation violations detected.${NC}"
fi

if [ $FAILED -gt 0 ]; then
    exit 1
fi
exit 0
