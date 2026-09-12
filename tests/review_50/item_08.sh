#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
bash tests/run_unit_tests.sh test_async_task
bash tests/run_unit_tests.sh test_review_snapshot
bash tests/review_50/item_07.sh
