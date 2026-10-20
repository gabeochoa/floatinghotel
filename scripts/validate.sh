#!/bin/bash
# Run the app briefly against the fixture repo and collect validation warnings.
# Requires GPU access (run from a GUI terminal, not tmux/ssh).
cd "$(dirname "$0")/.."
mkdir -p output/validation
[ -d tests/fixture_repo/.git ] || bash tests/create_fixture_repo.sh
output/floatinghotel.exe tests/fixture_repo \
    --test-mode \
    --test-script=tests/e2e_scripts/ui_audit_screenshots.e2e \
    --screenshot-dir=output/validation/screenshots \
    --validation-report=output/validation/report.json \
    --e2e-timeout=10 \
    2>&1 | tee output/validation/validate.log
echo ""
if grep -q "UI Validation Summary" output/validation/validate.log 2>/dev/null; then
    echo "--- Validation Results ---"
    sed -n '/=== UI Validation Summary/,/=== End Validation Summary ===/p' output/validation/validate.log
elif [ -f output/validation/report.json ]; then
    echo "Validation report: $(grep -c '"message"' output/validation/report.json 2>/dev/null || echo 0) unique violations"
    echo "See output/validation/report.json for details"
else
    echo "No validation output captured (app may have failed to start)"
fi
