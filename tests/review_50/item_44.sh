#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
repo_dir=$(mktemp -d /tmp/fh-review-44.XXXXXX)
trap 'rm -rf "$repo_dir"' EXIT
git -C "$repo_dir" init -q -b main
git -C "$repo_dir" config user.name 'Review 50'
git -C "$repo_dir" config user.email review50@example.invalid
swift tests/helpers/make_preview_images.swift "$repo_dir/image.png" "$repo_dir/after.png"
git -C "$repo_dir" add image.png
git -C "$repo_dir" commit -qm Baseline
mv "$repo_dir/after.png" "$repo_dir/image.png"
bash tests/run_unit_tests.sh test_image_view_state
output/floatinghotel.exe "$repo_dir" --test-mode --headless \
  --test-script=tests/review_50/item_44.e2e \
  --screenshot-dir=output/screenshots/review-50 --e2e-timeout=40
