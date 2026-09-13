#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
page_repo=$(mktemp -d /tmp/fh-review-file-pages.XXXXXX)
trap 'rm -rf "$page_repo"' EXIT
git -C "$page_repo" init -q -b main
git -C "$page_repo" config user.name 'Review test'
git -C "$page_repo" config user.email 'review@example.invalid'
perl -e 'for (1..15000) { print $_ == 1 ? "baseline source\n" : $_ == 4097 ? "second page marker\n" : $_ == 12000 ? "faraway needle\n" : "ordinary line $_\n" }' > "$page_repo/large.txt"
git -C "$page_repo" add .
git -C "$page_repo" commit -qm baseline
perl -e 'for (1..15000) { print $_ == 1 ? "historical source\n" : $_ == 4097 ? "second page marker\n" : $_ == 12000 ? "faraway needle\n" : "ordinary line $_\n" }' > "$page_repo/large.txt"
git -C "$page_repo" commit -qam latest
perl -e 'for (1..15000) { print $_ == 1 ? "working source\n" : $_ == 4097 ? "second page marker\n" : $_ == 12000 ? "faraway needle\n" : $_ == 15000 ? "END_OF_LARGE_FILE\n" : "ordinary line $_\n" }' > "$page_repo/large.txt"
output/floatinghotel.exe "$page_repo" --test-mode --headless --test-script=tests/review_50/item_02.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=50

python3 - <<'CHECK'
import json
from pathlib import Path
layout = json.loads(Path("output/screenshots/review_50/item_02_end_ready.json").read_text())
viewport = next(n["visible_rect"] for n in layout["nodes"] if n.get("name") == "diff_scroll" and n["rendered"])
row = next(r for r in layout["reading_rows"] if r["path"] == "large.txt" and r["line"] == 15000)
assert row["text"] == "END_OF_LARGE_FILE"
assert row["rect"]["y"] >= viewport["y"] - .5
assert row["rect"]["y"] + row["rect"]["height"] <= viewport["y"] + viewport["height"] + .5
print("Final source line is visible within subpixel layout tolerance")
CHECK
