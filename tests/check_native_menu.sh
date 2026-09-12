#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
task_output=$(nice -n 10 mktemp -d /tmp/floatinghotel-native-menu-test.XXXXXX)
nice -n 10 clang++ -std=c++23 -Wall -Wextra -Werror -fno-objc-arc \
    -framework AppKit src/platform/native_menu.mm tests/test_native_menu.mm \
    -o "$task_output/test_native_menu"
nice -n 10 "$task_output/test_native_menu"
