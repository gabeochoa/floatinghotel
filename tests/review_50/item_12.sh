#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
image_repo=$(mktemp -d /tmp/fh-review-image-async.XXXXXX)
trap 'rm -rf "$image_repo"' EXIT
git -C "$image_repo" init -q -b main
git -C "$image_repo" config user.name 'Review test'
git -C "$image_repo" config user.email 'review@example.invalid'
perl -e 'print pack("a2VVV", "BM", 12342, 0, 54), pack("V3v2V6", 40, 64, 64, 1, 24, 0, 12288, 0, 0, 0, 0), "\xff\0\0" x 4096' > "$image_repo/image.bmp"
printf 'before text\n' > "$image_repo/source.txt"
git -C "$image_repo" add .
git -C "$image_repo" commit -qm baseline
perl -e 'print pack("a2VVV", "BM", 12342, 0, 54), pack("V3v2V6", 40, 64, 64, 1, 24, 0, 12288, 0, 0, 0, 0), "\0\0\xff" x 4096' > "$image_repo/image.bmp"
printf 'after text\n' > "$image_repo/source.txt"
output/floatinghotel.exe "$image_repo" --test-mode --headless --test-script=tests/review_50/item_12.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
