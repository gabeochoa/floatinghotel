#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
blob_repo=$(mktemp -d /tmp/fh-review-blob-pages.XXXXXX)
trap 'rm -rf "$blob_repo"' EXIT
git -C "$blob_repo" init -q -b main
git -C "$blob_repo" config user.name 'Review test'
git -C "$blob_repo" config user.email 'review@example.invalid'
perl -e 'for (1..9000) { print $_ == 1 ? "baseline source\n" : $_ == 4097 ? "second page marker\n" : "ordinary line $_\n" }' > "$blob_repo/large.txt"
git -C "$blob_repo" add .
git -C "$blob_repo" commit -qm baseline
perl -e 'for (1..9000) { print $_ == 1 ? "historical source\n" : $_ == 4097 ? "second page marker\n" : "ordinary line $_\n" }' > "$blob_repo/large.txt"
git -C "$blob_repo" commit -qam latest
perl -e 'for (1..9000) { print $_ == 1 ? "working source\n" : $_ == 4097 ? "second page marker\n" : "ordinary line $_\n" }' > "$blob_repo/large.txt"
output/floatinghotel.exe "$blob_repo" --test-mode --headless --test-script=tests/review_50/item_04.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40 | tee "$blob_repo/.git/native.log"
grep -q 'blob page cache hit' "$blob_repo/.git/native.log"
