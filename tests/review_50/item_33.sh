#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
lfs_repo=$(mktemp -d /tmp/fh-review-lfs.XXXXXX)
trap 'rm -rf "$lfs_repo"' EXIT
git -C "$lfs_repo" init -q -b main
git -C "$lfs_repo" config user.name 'Review test'
git -C "$lfs_repo" config user.email 'review@example.invalid'
old_oid=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
new_oid=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
printf 'version https://git-lfs.github.com/spec/v1\noid sha256:%s\nsize 3\n' "$old_oid" > "$lfs_repo/asset.dat"
git -C "$lfs_repo" add .
git -C "$lfs_repo" commit -qm baseline
mkdir -p "$lfs_repo/.git/lfs/objects/aa/aa"
printf abc > "$lfs_repo/.git/lfs/objects/aa/aa/$old_oid"
printf 'version https://git-lfs.github.com/spec/v1\noid sha256:%s\nsize 1024\n' "$new_oid" > "$lfs_repo/asset.dat"
output/floatinghotel.exe "$lfs_repo" --test-mode --headless --test-script=tests/review_50/item_33.e2e --screenshot-dir=output/screenshots/review_50 --e2e-timeout=40
