#!/bin/bash
# Build a throwaway git repo with a chosen amount of history and dirt, for
# stress-testing floatinghotel against large status lists and logs.
#
#   scripts/make_stress_repo.sh OUT_DIR [--tracked N] [--modified N]
#       [--staged N] [--untracked N] [--commits N] [--force]
#
# Defaults: 1000 tracked files, 500 modified, 20 staged, 500 untracked, 100
# commits. An existing OUT_DIR with a marker matching the same parameters is
# reused; --force rebuilds it.
set -euo pipefail

OUT="${1:?usage: $0 OUT_DIR [options]}"; shift
TRACKED=1000; MODIFIED=500; STAGED=20; UNTRACKED=500; COMMITS=100; FORCE=false
while [[ $# -gt 0 ]]; do
    case $1 in
        --tracked)   TRACKED="$2"; shift 2 ;;
        --modified)  MODIFIED="$2"; shift 2 ;;
        --staged)    STAGED="$2"; shift 2 ;;
        --untracked) UNTRACKED="$2"; shift 2 ;;
        --commits)   COMMITS="$2"; shift 2 ;;
        --force)     FORCE=true; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

SIG="tracked=$TRACKED modified=$MODIFIED staged=$STAGED untracked=$UNTRACKED commits=$COMMITS"
if [ "$FORCE" = false ] && [ -f "$OUT/.stress_sig" ] && [ "$(cat "$OUT/.stress_sig")" = "$SIG" ]; then
    echo "$OUT"
    exit 0
fi

rm -rf "$OUT"
mkdir -p "$OUT"
cd "$OUT"
git init -q -b main
git config user.email "stress@floatinghotel.dev"
git config user.name "Stress Bot"
git config commit.gpgsign false

# Tracked files spread over a few directories so the sidebar's filename/dir
# split and sort paths get exercised.
dirs=(src src/core src/ui lib tests docs)
for ((i = 0; i < TRACKED; i++)); do
    d="${dirs[$((i % ${#dirs[@]}))]}"
    mkdir -p "$d"
    printf '// file %d\nint value_%d() { return %d; }\n' "$i" "$i" "$i" > "$d/file_$i.cpp"
done
git add -A
git commit -q -m "Add $TRACKED files"

# History: each commit touches the same file so the log is long but cheap.
for ((c = 1; c < COMMITS; c++)); do
    echo "change $c" >> CHANGELOG.md
    git add CHANGELOG.md
    git commit -q -m "Change $c: adjust the thing that needed adjusting"
done

# Dirt. Modified files are the first MODIFIED tracked ones; staged files are
# the next STAGED after those; untracked files are new.
i=0
for ((n = 0; n < MODIFIED && i < TRACKED; n++, i++)); do
    d="${dirs[$((i % ${#dirs[@]}))]}"
    echo "// modified" >> "$d/file_$i.cpp"
done
for ((n = 0; n < STAGED && i < TRACKED; n++, i++)); do
    d="${dirs[$((i % ${#dirs[@]}))]}"
    echo "// staged" >> "$d/file_$i.cpp"
    git add "$d/file_$i.cpp"
done
# Spread across the tracked directories: git status collapses a directory of
# nothing but untracked files into one entry, which is no stress at all.
for ((n = 0; n < UNTRACKED; n++)); do
    d="${dirs[$((n % ${#dirs[@]}))]}"
    echo "new $n" > "$d/new_$n.txt"
done

echo "$SIG" > .stress_sig
echo ".stress_sig" >> .git/info/exclude
echo "$OUT"
