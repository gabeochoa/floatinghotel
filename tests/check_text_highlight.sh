#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
nice -n 10 python3 tests/text_highlight.py --output "${1:-output/text-highlight}"
