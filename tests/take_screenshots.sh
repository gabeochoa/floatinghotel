#!/bin/bash
# take_screenshots.sh — Launches the app and captures screenshots of each UI state.
# Run this from Terminal.app or Cursor terminal (NOT from tmux/SSH).
#
# Usage: ./tests/take_screenshots.sh [repo_path]
#   repo_path defaults to the floatinghotel repo itself.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
EXECUTABLE="$PROJECT_DIR/output/floatinghotel.exe"
SCREENSHOT_DIR="$PROJECT_DIR/output/screenshots/ui_audit"
REPO="${1:-$PROJECT_DIR}"

if [ ! -f "$EXECUTABLE" ]; then
    echo "Building first..."
    cd "$PROJECT_DIR" && make
fi

rm -rf "$SCREENSHOT_DIR"
mkdir -p "$SCREENSHOT_DIR"

echo "=== Floatinghotel Screenshot Capture ==="
echo "Repo: $REPO"
echo "Screenshots: $SCREENSHOT_DIR"
echo ""

# Launch the app
"$EXECUTABLE" "$REPO" &
APP_PID=$!
echo "Launched app (PID: $APP_PID)"

# Wait for the window to appear
echo "Waiting for window..."
for i in $(seq 1 20); do
    if /usr/bin/osascript -e 'tell application "System Events" to return (name of every window of every process whose name contains "floatinghotel")' 2>/dev/null | grep -q "floatinghotel"; then
        echo "Window found!"
        break
    fi
    sleep 0.5
done

# Give the app time to render its first frame and load git data
sleep 2

take_screenshot() {
    local name="$1"
    local path="$SCREENSHOT_DIR/${name}.png"
    screencapture -x -o -l "$(osascript -e 'tell application "System Events" to return id of first window of first process whose name contains "floatinghotel"' 2>/dev/null)" "$path" 2>/dev/null || screencapture -x "$path" 2>/dev/null
    echo "  Captured: $name"
}

send_key() {
    /usr/bin/osascript -e "
        tell application \"System Events\"
            tell process \"floatinghotel\"
                set frontmost to true
                keystroke \"$1\"
            end tell
        end tell
    " 2>/dev/null
}

send_key_code() {
    /usr/bin/osascript -e "
        tell application \"System Events\"
            tell process \"floatinghotel\"
                set frontmost to true
                key code $1
            end tell
        end tell
    " 2>/dev/null
}

click_at() {
    /usr/bin/osascript -e "
        tell application \"System Events\"
            tell process \"floatinghotel\"
                set frontmost to true
                click at {$1, $2}
            end tell
        end tell
    " 2>/dev/null
}

echo ""
echo "Taking screenshots..."

# 1. Initial state — app with git repo loaded (Changes tab, sidebar visible)
take_screenshot "01_initial_state"

# 2. Click on a file to show diff
sleep 0.5
# Try clicking on a file in the sidebar (approximate position: sidebar area)
click_at "140" "200"
sleep 1
take_screenshot "02_file_selected_diff_view"

# 3. Click on a second file
click_at "140" "230"
sleep 1
take_screenshot "03_second_file_selected"

# 4. Open the File menu
click_at "30" "14"
sleep 0.5
take_screenshot "04_file_menu_open"

# 5. Close the menu (press Escape)
send_key_code 53  # Escape
sleep 0.3

# 6. Open the View menu
click_at "70" "14"
sleep 0.5
take_screenshot "05_view_menu_open"

# 7. Close the menu
send_key_code 53
sleep 0.3

# 8. Switch to Refs tab (click on the Refs tab in sidebar)
click_at "200" "60"
sleep 1
take_screenshot "06_refs_tab_branches"

# 9. Switch back to Changes tab
click_at "100" "60"
sleep 0.5

# 10. Click on a commit in the commit log
click_at "140" "500"
sleep 1
take_screenshot "07_commit_selected"

# 11. Click a different commit
click_at "140" "540"
sleep 1
take_screenshot "08_different_commit_selected"

# 12. Resize window smaller
/usr/bin/osascript -e '
    tell application "System Events"
        tell process "floatinghotel"
            set size of first window to {800, 500}
        end tell
    end tell
' 2>/dev/null
sleep 1
take_screenshot "09_small_window"

# 13. Resize window larger
/usr/bin/osascript -e '
    tell application "System Events"
        tell process "floatinghotel"
            set size of first window to {1400, 900}
        end tell
    end tell
' 2>/dev/null
sleep 1
take_screenshot "10_large_window"

# 14. Full screenshot with everything visible
sleep 0.5
take_screenshot "11_final_state"

# Clean up
echo ""
echo "Shutting down app..."
kill "$APP_PID" 2>/dev/null
wait "$APP_PID" 2>/dev/null || true

echo ""
echo "=== Done ==="
echo "Screenshots saved to: $SCREENSHOT_DIR"
echo ""
ls -la "$SCREENSHOT_DIR"
