#!/usr/bin/env bash
# Repo tabs: Cmd+digit / Cmd+Shift+] switching, refresh when returning to a tab
# that changed in the background, and double-click (not single-click) checkout.
set -euo pipefail
cd "$(dirname "$0")/.."
root=$(mktemp -d /tmp/fh-repo-tabs.XXXXXX)
trap 'rm -rf "$root"' EXIT
for name in alpha beta; do
    repo="$root/$name"
    mkdir "$repo"
    git -C "$repo" init -q -b main
    git -C "$repo" config user.name 'Tab test'
    git -C "$repo" config user.email 'tabs@example.invalid'
    printf '%s\n' "$name" > "$repo/$name-only.txt"
    git -C "$repo" add .
    git -C "$repo" commit -qm "$name baseline"
    printf 'dirty\n' >> "$repo/$name-only.txt"
    printf '%s\n' "$repo" >> "$root/repos"
done
git -C "$root/beta" branch feature/other
sed "s|__BETA_FILE__|$root/beta/background-change.txt|" tests/repo_tab_switching.e2e > "$root/script.e2e"
FH_TAB_REFRESH_AFTER_MS=0 FH_NAVIGATION_REPOS="$root/repos" output/floatinghotel.exe --test-mode --headless \
    --test-script="$root/script.e2e" --screenshot-dir=output/screenshots/repo_tabs --e2e-timeout=40
test "$(git -C "$root/beta" branch --show-current)" = feature/other
