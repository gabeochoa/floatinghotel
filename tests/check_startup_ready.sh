#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
startup_root=$(mktemp -d /tmp/fh-startup-ready.XXXXXX)
trap 'rm -rf "$startup_root"' EXIT
for number in 1 2 3; do
    repo="$startup_root/inactive$number"
    mkdir "$repo"
    git -C "$repo" init -q -b main
    git -C "$repo" config user.name 'Review test'
    git -C "$repo" config user.email 'review@example.invalid'
    printf 'before\n' > "$repo/slow.txt"
    printf 'slow.txt diff=slow\n' > "$repo/.gitattributes"
    git -C "$repo" add .
    git -C "$repo" commit -qm baseline
    printf 'after\n' > "$repo/slow.txt"
    git -C "$repo" config diff.slow.textconv "sh '$PWD/tests/startup_slow_filter.sh' '$startup_root/filter$number'"
    printf '%s\n' "$repo" >> "$startup_root/repos"
done
repo="$startup_root/active"
mkdir "$repo"
git -C "$repo" init -q -b main
git -C "$repo" config user.name 'Review test'
git -C "$repo" config user.email 'review@example.invalid'
printf 'before\n' > "$repo/app.txt"
git -C "$repo" add .
git -C "$repo" commit -qm baseline
printf 'timed patch content\n' > "$repo/app.txt"
git -C "$repo" commit -qam 'Timed commit'
printf '%s\n' "$repo" >> "$startup_root/repos"
if [[ ${1:-} == --headless-timing ]]; then
    FH_NAVIGATION_TIMING=1 FH_NAVIGATION_PREFETCH=1 FH_NAVIGATION_REPOS="$startup_root/repos" \
        output/floatinghotel.exe --test-mode --headless --test-script=tests/startup_timing.e2e \
        --screenshot-dir=output/screenshots/startup/timing --e2e-timeout=40
    test ! -e "$startup_root/filter1"
    test ! -e "$startup_root/filter2"
    test ! -e "$startup_root/filter3"
    exit
fi
FH_NAVIGATION_TIMING=1 FH_NAVIGATION_PREFETCH=1 FH_NAVIGATION_REPOS="$startup_root/repos" \
    output/floatinghotel.exe --test-mode --test-script=tests/startup_ready.e2e \
    --screenshot-dir=output/screenshots/startup --e2e-timeout=40 | tee "$startup_root/native.log"
grep -q 'NAV before_present visible=0 key=0 .*frontmost=0' "$startup_root/native.log"
grep -q 'NAV presented visible=1 key=1' "$startup_root/native.log"
grep -q 'NAV startup_ready loaded_tabs=1 active_commits=2' "$startup_root/native.log"
test "$(wc -l < "$startup_root/filter1" | tr -d ' ')" = 2
test ! -e "$startup_root/filter2"
test ! -e "$startup_root/filter3"
: > "$startup_root/empty"
FH_NAVIGATION_TIMING=1 FH_NAVIGATION_PREFETCH=1 FH_NAVIGATION_REPOS="$startup_root/empty" \
    output/floatinghotel.exe --test-mode --test-script=tests/startup_empty.e2e \
    --screenshot-dir=output/screenshots/startup/welcome --e2e-timeout=40 | tee "$startup_root/welcome.log"
grep -q 'NAV before_present visible=0 key=0 .*frontmost=0' "$startup_root/welcome.log"
grep -q 'NAV presented visible=1 key=1' "$startup_root/welcome.log"
grep -q 'NAV startup_ready loaded_tabs=0 active_commits=0' "$startup_root/welcome.log"
mkdir "$startup_root/not-a-repository"
printf '%s\n' "$startup_root/not-a-repository" > "$startup_root/invalid"
FH_NAVIGATION_TIMING=1 FH_NAVIGATION_PREFETCH=1 FH_NAVIGATION_REPOS="$startup_root/invalid" \
    output/floatinghotel.exe --test-mode --test-script=tests/startup_invalid.e2e \
    --screenshot-dir=output/screenshots/startup/invalid --e2e-timeout=40 | tee "$startup_root/invalid.log"
grep -q 'NAV before_present visible=0 key=0 .*frontmost=0' "$startup_root/invalid.log"
grep -q 'NAV presented visible=1 key=1' "$startup_root/invalid.log"
grep -q 'NAV startup_ready loaded_tabs=1 active_commits=0' "$startup_root/invalid.log"
! grep -q 'NAV premature_' "$startup_root/native.log" "$startup_root/welcome.log" "$startup_root/invalid.log"
