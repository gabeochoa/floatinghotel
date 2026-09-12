#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
bash tests/run_unit_tests.sh test_repository_lock
bash tests/review_50/item_08.sh
