#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
bash tests/run_unit_tests.sh test_process
bash tests/review_50/item_01.sh
bash tests/check_async_commit.sh
