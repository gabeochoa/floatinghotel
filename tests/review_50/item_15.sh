#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
result_dir="output/review_50"
mkdir -p "$result_dir"
result_file="$result_dir/item_15_results.log"
repo_dir=$(mktemp -d /tmp/fh-review-15.XXXXXX)
trap 'rm -rf "$repo_dir"' EXIT
git -C "$repo_dir" init -q -b main
git -C "$repo_dir" config user.name 'Review 50'
git -C "$repo_dir" config user.email review50@example.invalid
for n in $(seq 1 6500); do printf 'int value_%05d = %d;\n' "$n" "$n"; done > "$repo_dir/huge.cpp"
git -C "$repo_dir" add huge.cpp
git -C "$repo_dir" commit -qm Baseline
printf 'history body\n' > "$repo_dir/history.txt"
awk 'BEGIN { print "Large history message\n"; for(i=1;i<=9000;i++) print "Message line " i; print "FULL_MESSAGE_END" }' > "$repo_dir/message.txt"
git -C "$repo_dir" add history.txt
git -C "$repo_dir" commit -q -F "$repo_dir/message.txt"
awk '{ if (NR % 40 == 0) printf "int changed_%05d = %d;\n", NR, NR; else print }' "$repo_dir/huge.cpp" > "$repo_dir/huge.next"
mv "$repo_dir/huge.next" "$repo_dir/huge.cpp"
if [ "${FH_SKIP_BUILD:-0}" != 1 ]; then
  nice -n 10 make -j1 OPT=-O2
fi
nice -n 10 bash tests/review_50/item_15_build_modes.sh
nice -n 10 bash tests/review_50/item_15_test_filter.sh
set -o pipefail
output/floatinghotel.exe "$repo_dir" --test-mode --headless \
  --test-script=tests/review_50/item_15.e2e \
  --screenshot-dir=output/screenshots/review-50 --e2e-timeout=90 \
  2>&1 | tee "$result_file"
nice -n 10 bash tests/review_50/item_15_search_caps.sh
