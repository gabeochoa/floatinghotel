#!/bin/bash
# E2E Screenshot Runner - to be executed from Cursor terminal (has GPU access)
set -e

cd /Users/gabeochoa/p/floatinghotel

echo "=== Floatinghotel E2E Screenshot Runner ==="
echo ""

# Ensure fixture repo exists
if [ ! -d "tests/fixture_repo/.git" ]; then
    echo "Creating fixture repo..."
    bash tests/create_fixture_repo.sh
fi

# Ensure binary is built
if [ ! -f "output/floatinghotel.exe" ]; then
    echo "Building..."
    make
fi

# Clean and create screenshot dirs
rm -rf output/screenshots/e2e_audit
mkdir -p output/screenshots/e2e_audit

# Run each E2E script
SCRIPTS=(
    "ui_audit_screenshots"
    "sidebar_interactions"
    "menu_bar"
    "diff_viewer"
    "status_toolbar"
)

PASSED=0
FAILED=0

for name in "${SCRIPTS[@]}"; do
    script="tests/e2e_scripts/${name}.e2e"
    screenshot_dir="output/screenshots/e2e_audit/${name}"
    mkdir -p "$screenshot_dir"

    echo "--- Running: $name ---"
    if ./output/floatinghotel.exe tests/fixture_repo \
        --test-mode \
        --test-script="$script" \
        --screenshot-dir="$screenshot_dir" \
        --e2e-timeout=30 2>&1; then
        echo "  [PASS] $name"
        PASSED=$((PASSED + 1))
    else
        echo "  [FAIL] $name (exit: $?)"
        FAILED=$((FAILED + 1))
    fi

    SC_COUNT=$(ls "$screenshot_dir"/*.png 2>/dev/null | wc -l | tr -d ' ')
    echo "  Screenshots: $SC_COUNT"
    echo ""
done

echo "=== Summary ==="
echo "Passed: $PASSED  Failed: $FAILED"
echo ""

# List all screenshots
TOTAL=$(find output/screenshots/e2e_audit -name "*.png" 2>/dev/null | wc -l | tr -d ' ')
echo "Total screenshots: $TOTAL"
find output/screenshots/e2e_audit -name "*.png" -ls 2>/dev/null

echo ""
echo "DONE"
