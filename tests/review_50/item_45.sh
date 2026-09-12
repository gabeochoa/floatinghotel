#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
repo_dir=$(mktemp -d /tmp/fh-review-45.XXXXXX)
trap 'rm -rf "$repo_dir"' EXIT
git -C "$repo_dir" init -q -b main
git -C "$repo_dir" config user.name 'Review 50'
git -C "$repo_dir" config user.email review50@example.invalid
printf 'base\n' > "$repo_dir/README.md"
git -C "$repo_dir" add README.md
git -C "$repo_dir" commit -qm Baseline
{
  printf '# Review notes\n\n'
  printf -- '- inspect safely\n\n'
  printf '![remote](https://example.invalid/tracker.png)\n\n'
  printf '<script>alert(1)</script>\n\n'
  printf '```\nliteral code\n```\n'
  for ((i=0; i<100; i++)); do printf 'A long paragraph needs room to wrap without hiding the next line. '; done
  printf '\nAfter wrapped paragraph\n'
  for ((i=0; i<160; i++)); do printf 'Markdown row %03d\n' "$i"; done
  printf 'End of preview\n'
} > "$repo_dir/README.md"
bash tests/run_unit_tests.sh test_markdown_preview
output/floatinghotel.exe "$repo_dir" --test-mode --headless \
  --test-script=tests/review_50/item_45.e2e \
  --screenshot-dir=output/screenshots/review-50 --e2e-timeout=40
