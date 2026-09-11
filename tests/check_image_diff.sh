#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
image_repo=$(mktemp -d /tmp/floatinghotel-image-diff.XXXXXX)
trap 'rm -rf "$image_repo"' EXIT
git -C "$image_repo" init -q
git -C "$image_repo" config user.name 'UI test'
git -C "$image_repo" config user.email 'ui-test@example.invalid'
swift tests/helpers/make_preview_images.swift "$image_repo/image.png" "$image_repo/after.png"
git -C "$image_repo" add image.png
git -C "$image_repo" commit -qm baseline
mv "$image_repo/after.png" "$image_repo/image.png"
output/floatinghotel.exe "$image_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_20_images.e2e \
  --screenshot-dir=output/screenshots/improvements --e2e-timeout=40
