#!/bin/bash
# Floatinghotel E2E Test Runner
# Uses afterhours E2E testing plugin with --test-mode and .e2e scripts.
# Runs all tests in a single window (batch mode) by default.
#
# Usage: ./tests/run_e2e.sh [OPTIONS] [SCRIPT_FILTER]
#   -t, --timeout SEC   Set timeout per script (default: 30)
#   -d, --dir DIR       E2E scripts directory (default: tests/e2e_scripts)
#   --isolate           Run each script in its own process (old behavior)
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
ISOLATE=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--timeout) TIMEOUT="$2"; shift 2 ;;
        -d|--dir) E2E_SCRIPTS_DIR="$2"; shift 2 ;;
        --isolate) ISOLATE=true; shift ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS] [FILTER]"
            echo "  -t, --timeout SEC   Timeout per script (default: 30)"
            echo "  -d, --dir DIR       Scripts directory"
            echo "  --isolate           Run each script in its own process"
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

# Validation report directory
VALIDATION_DIR="$OUTPUT_DIR/validation"
mkdir -p "$VALIDATION_DIR"

# Find matching scripts
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

if [ "$ISOLATE" = true ]; then
    echo "Mode: isolated (one window per script)"
else
    echo "Mode: batch (single window)"
fi
echo ""

# ============================================================
# Batch mode: run all scripts in a single process/window
# ============================================================
if [ "$ISOLATE" = false ]; then
    # If there's a filter, copy matching scripts to a temp dir
    if [ -n "$FILTER" ]; then
        BATCH_DIR=$(mktemp -d)
        trap "rm -rf $BATCH_DIR" EXIT
        for script in "${SCRIPTS[@]}"; do
            cp "$script" "$BATCH_DIR/"
        done
    else
        BATCH_DIR="$E2E_SCRIPTS_DIR"
    fi

    BATCH_LOG="$VALIDATION_DIR/batch.log"

    echo -e "${BLUE}Running ${#SCRIPTS[@]} scripts in single window...${NC}"
    echo ""

    if "$EXECUTABLE" "$REPO_PATH" \
        --test-mode \
        --test-script-dir="$BATCH_DIR" \
        --screenshot-dir="$SCREENSHOT_DIR" \
        --e2e-timeout="$TIMEOUT" \
        2>&1 | tee "$BATCH_LOG"; then
        EXIT_CODE=0
    else
        EXIT_CODE=$?
    fi

    # Parse results from the framework's batch summary
    PASSED=$(grep 'Scripts passed:' "$BATCH_LOG" 2>/dev/null | grep -o '[0-9]*' || echo "0")
    FAILED=$(grep 'Scripts failed:' "$BATCH_LOG" 2>/dev/null | grep -o '[0-9]*' || echo "0")
    TOTAL_RUN=$(grep 'Scripts run:' "$BATCH_LOG" 2>/dev/null | grep -o '[0-9]*' || echo "0")

    echo ""
    echo "=============================================="
    echo "   E2E Test Summary"
    echo "=============================================="
    echo ""
    echo "  Total:   $TOTAL_RUN"
    echo -e "  Passed:  ${GREEN}$PASSED${NC}"
    echo -e "  Failed:  ${RED}$FAILED${NC}"
    echo ""

    TOTAL_SC=$(find "$SCREENSHOT_DIR" -name "*.png" 2>/dev/null | wc -l | tr -d ' ')
    echo "Screenshots: $TOTAL_SC saved to $SCREENSHOT_DIR"

    if [ "$FAILED" -gt 0 ] || [ "$EXIT_CODE" -ne 0 ]; then
        exit 1
    fi
    exit 0
fi

# ============================================================
# Isolated mode: run each script in its own process/window
# ============================================================
PASSED=0
FAILED=0

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

    SC_COUNT=$(ls "$SCRIPT_SCREENSHOT_DIR"/*.png 2>/dev/null | wc -l | tr -d ' ')
    echo "  Screenshots: $SC_COUNT"

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

TOTAL_SC=$(find "$SCREENSHOT_DIR" -name "*.png" 2>/dev/null | wc -l | tr -d ' ')
echo "Total screenshots: $TOTAL_SC"

# Validation summary across all scripts
echo ""
echo "=============================================="
echo "   Validation Summary"
echo "=============================================="
echo ""
echo "Reports saved to: $VALIDATION_DIR/"

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
