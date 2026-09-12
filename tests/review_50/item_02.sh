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
