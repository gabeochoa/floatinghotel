#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
graph_repo=$(mktemp -d /tmp/floatinghotel-graph.XXXXXX)
trap 'rm -rf "$graph_repo"' EXIT
git -C "$graph_repo" init -q -b main
git -C "$graph_repo" config user.name 'UI test'
git -C "$graph_repo" config user.email 'ui-test@example.invalid'
printf 'root\n' > "$graph_repo/root.txt"
git -C "$graph_repo" add .
git -C "$graph_repo" commit -qm 'Shared root'
git -C "$graph_repo" checkout -qb feature
printf 'feature\n' > "$graph_repo/feature.txt"
git -C "$graph_repo" add .
git -C "$graph_repo" commit -qm 'Feature work'
git -C "$graph_repo" checkout -q main
printf 'main\n' > "$graph_repo/main.txt"
git -C "$graph_repo" add .
git -C "$graph_repo" commit -qm 'Main work'
git -C "$graph_repo" merge -q --no-ff feature -m 'Merge feature'
git -C "$graph_repo" commit -q --allow-empty -m 'Empty checkpoint'
capture_dir=${1:-output/screenshots/improvements}
mkdir -p "$capture_dir"
git -C "$graph_repo" log --all --graph --format='%h %p %s' > "$capture_dir/git-graph.txt"
output/floatinghotel.exe "$graph_repo" --test-mode --headless \
  --test-script=tests/navigation_scripts/improvement_29_graph.e2e \
  --screenshot-dir="$capture_dir" --e2e-timeout=40
