#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
project_dir="$PWD"
fixture_dir=$(mktemp -d /tmp/fh-review-build-modes.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
mkdir -p "$fixture_dir/src" "$fixture_dir/resources"
cp tests/review_50/build_mode.cpp "$fixture_dir/src/main.cpp"
build_mode() {
    nice -n 10 make -s -j1 -C "$fixture_dir" -f "$project_dir/makefile" \
        CXX=clang++ 'CXXFLAGS=$(OPT)' INCLUDES= LDFLAGS= \
        MAIN_SRC=src/main.cpp MAIN_MM_SRC= \
        'MAIN_OBJS=$(OBJ_DIR)/main/main.o' "OPT=$1"
    actual=$("$fixture_dir/output/floatinghotel.exe")
    if [ "$actual" != "$2" ]; then
        printf 'Expected %s for %s, got %s\n' "$2" "$1" "$actual" >&2
        exit 1
    fi
}
build_mode -O0 unoptimized
build_mode -O2 optimized
build_mode -O0 unoptimized
build_mode -O2 optimized
printf 'O0/O2/O0/O2 executable switching passed\n'
