#!/bin/bash
# Capture screenshots of the diff view mock using an in-process WebKit1 WebView
# (tests/wk1_shot.swift). This works in restricted environments where Chrome's
# subprocess renderer is blocked by the Mach bootstrap sandbox.
#
# Usage: tests/shot_mock.sh [html-file] [out-dir]
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
HTML="${1:-$PROJECT_DIR/docs/mocks/diff_view_mock.html}"
OUT="${2:-$PROJECT_DIR/output/screenshots/mock}"
BIN=/tmp/wk1_shot
W=1280; H=800

# Build the shooter if missing or stale
if [ ! -x "$BIN" ] || [ "$SCRIPT_DIR/wk1_shot.swift" -nt "$BIN" ]; then
  echo "Compiling wk1_shot..."
  swiftc -O "$SCRIPT_DIR/wk1_shot.swift" -o "$BIN" 2>/dev/null
fi

mkdir -p "$OUT"
URL="file://$HTML"

# name:view pairs matching the mock's data-view attributes
VIEWS=(
  "review_changes:review"
  "review_staged:staged"
  "sidebyside:sbs"
  "submodule:submodule"
  "refs:refs"
  "commit_detail:commit"
)

for pair in "${VIEWS[@]}"; do
  name="${pair%%:*}"; view="${pair##*:}"
  js="var b=document.querySelector('.mock-switcher button[data-view=\"$view\"]'); if(b){setView(b);} var s=document.querySelector('.mock-switcher'); if(s)s.style.display='none';"
  "$BIN" "$URL" "$OUT/mock_${name}.png" "$W" "$H" "$js" 2>&1 | sed "s/^/  [$name] /"
done

echo "Screenshots in: $OUT"
ls -la "$OUT"/mock_*.png
