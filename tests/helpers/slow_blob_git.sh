#!/usr/bin/env bash
set -euo pipefail
for argument in "$@"; do
  case "$argument" in
    *:source.cpp) sleep 3 ;;
  esac
done
exec "${FH_REAL_GIT:?}" "$@"
