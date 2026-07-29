#!/usr/bin/env bash
# shot.sh - quick single-screenshot helper for iterating on the UI.
#
# Reads e2e commands from stdin, drives one headless window, and writes
# output/screenshots/shot/<name>.png (a real rendered frame you can open).
#
# Usage:
#   tests/shot.sh <name> [repo] <<'EOF'
#   <e2e commands...>
#   EOF
#
#   repo defaults to tests/fixture_repo. Use "." for this repo, or any git dir.
#   A `resize 1280 720 / wait_for_refresh / wait_frames 30` preamble is added
#   automatically unless the script already contains a `resize` line, and a
#   trailing `screenshot <name>` is appended unless the script screenshots.
#
# Examples:
#   tests/shot.sh initial <<'EOF'
#   EOF
#
#   tests/shot.sh diff_view . <<'EOF'
#   click_ui commit_row
#   wait_frames 20
#   EOF
set -euo pipefail
cd "$(dirname "$0")/.."

NAME="${1:?usage: shot.sh <name> [repo] <<EOF ... EOF}"
REPO="${2:-tests/fixture_repo}"
EXE="output/floatinghotel.exe"
DIR="output/screenshots/shot"
mkdir -p "$DIR"
[ -x "$EXE" ] || { echo "build first: make" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
SCRIPT="$TMP/$NAME.e2e"
BODY="$(cat || true)"

# Preamble: fixture repos need make_test_repo; real repos just need a refresh.
if ! grep -q '^resize' <<<"$BODY"; then
  if [ "$REPO" = "tests/fixture_repo" ]; then echo "make_test_repo"; fi
  printf 'resize 1280 720\nwait_for_refresh\nwait_frames 30\n'
fi > "$SCRIPT"
printf '%s\n' "$BODY" >> "$SCRIPT"
grep -q '^screenshot' "$SCRIPT" || echo "screenshot $NAME" >> "$SCRIPT"

"$EXE" "$REPO" --test-mode --headless \
  --test-script="$SCRIPT" --screenshot-dir="$DIR" --e2e-timeout=25 \
  >/dev/null 2>&1 || true

OUT="$DIR/$NAME.png"
[ -f "$OUT" ] && echo "$OUT" || { echo "NO SCREENSHOT (script:$SCRIPT)" >&2; cat "$SCRIPT" >&2; exit 1; }
