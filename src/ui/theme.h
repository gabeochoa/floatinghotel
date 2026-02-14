#pragma once

#include <afterhours/src/drawing_helpers.h>

namespace theme {

using Color = afterhours::Color;

// Window chrome
inline Color WINDOW_BG       = {30, 30, 30, 255};     // #1E1E1E
inline Color SIDEBAR_BG      = {37, 37, 38, 255};     // #252526
inline Color PANEL_BG        = {30, 30, 30, 255};     // #1E1E1E
inline Color BORDER          = {58, 58, 58, 255};     // #3A3A3A

// Text
inline Color TEXT_PRIMARY     = {204, 204, 204, 255};  // #CCCCCC
inline Color TEXT_SECONDARY   = {128, 128, 128, 255};  // #808080
inline Color TEXT_ACCENT      = {78, 154, 6, 255};

// Status badges
inline Color STATUS_MODIFIED  = {227, 179, 65, 255};   // Yellow
inline Color STATUS_ADDED     = {87, 166, 74, 255};    // Green
inline Color STATUS_DELETED   = {220, 76, 71, 255};    // Red
inline Color STATUS_RENAMED   = {78, 154, 220, 255};   // Blue
inline Color STATUS_UNTRACKED = {128, 128, 128, 255};  // Gray
inline Color STATUS_CONFLICT  = {220, 140, 50, 255};   // Orange

// Diff colors
inline Color DIFF_ADD_BG      = {13, 17, 23, 255};
inline Color DIFF_ADD_TEXT     = {126, 231, 135, 255};
inline Color DIFF_DEL_BG      = {61, 17, 23, 255};
inline Color DIFF_DEL_TEXT     = {255, 123, 114, 255};
inline Color DIFF_HUNK_HEADER = {78, 154, 220, 255};

// Interactive
inline Color BUTTON_PRIMARY   = {0, 122, 204, 255};    // Blue
inline Color BUTTON_SECONDARY = {58, 58, 58, 255};
inline Color HOVER_BG         = {44, 44, 44, 255};
inline Color SELECTED_BG      = {4, 57, 94, 255};
inline Color FOCUS_RING       = {0, 122, 204, 255};

// Toolbar
inline Color TOOLBAR_BG       = {37, 37, 38, 255};     // #252526 (same as sidebar)
inline Color TOOLBAR_BTN_HOVER= {44, 44, 44, 255};     // #2C2C2C
inline Color TOOLBAR_BTN_ACTIVE={0, 122, 204, 255};     // #007ACC (blue flash on press)
inline Color TOOLBAR_BTN_DISABLED={90, 90, 90, 255};    // #5A5A5A

// Decoration badges (commit log branch/tag labels)
inline Color BADGE_BRANCH_BG  = {0, 122, 204, 255};    // #007ACC (local branch)
inline Color BADGE_HEAD_BG    = {87, 166, 74, 255};     // #57A64A (HEAD)
inline Color BADGE_REMOTE_BG  = {78, 154, 220, 255};    // #4E9ADC (remote branch)
inline Color BADGE_TAG_BG     = {85, 85, 85, 255};      // #555555 (tag)
inline Color BADGE_TAG_TEXT   = {204, 204, 204, 255};   // #CCCCCC (tag text)

// Row separator
inline Color ROW_SEPARATOR    = {42, 42, 42, 255};      // #2A2A2A

// Status bar
inline Color STATUS_BAR_BG    = {0, 122, 204, 255};    // Blue (#007ACC)
inline Color STATUS_BAR_TEXT  = {255, 255, 255, 255};   // White
inline Color STATUS_BAR_CLEAN = {115, 201, 145, 255};   // Green dot (#73C991)
inline Color STATUS_BAR_DIRTY = {227, 179, 65, 255};    // Yellow dot (#E3B341)
inline Color STATUS_BAR_DETACHED_BG = {204, 102, 51, 255}; // Warning orange (#CC6633)
inline Color STATUS_BAR_BTN_HOVER = {255, 255, 255, 25};  // Subtle white hover

// Layout constants
namespace layout {
constexpr int MENU_BAR_HEIGHT = 28;
constexpr int TOOLBAR_HEIGHT = 36;
constexpr int TOOLBAR_BUTTON_HEIGHT = 28;
constexpr int TOOLBAR_BUTTON_HPAD = 6;
constexpr int TOOLBAR_BUTTON_VPAD = 4;
constexpr int TOOLBAR_SEP_WIDTH = 1;
constexpr int TOOLBAR_SEP_HEIGHT = 20;
constexpr int TOOLBAR_SEP_MARGIN = 6;
constexpr int STATUS_BAR_HEIGHT = 24;
constexpr int SIDEBAR_DEFAULT_WIDTH = 280;
constexpr int SIDEBAR_MIN_WIDTH = 180;
constexpr int FILE_ROW_HEIGHT = 24;
constexpr int COMMIT_ROW_HEIGHT = 40;
constexpr int PADDING = 8;
constexpr int SMALL_PADDING = 4;
constexpr float FONT_SIZE_MONO = 14.0f;    // Monospace for code/diffs
constexpr float FONT_SIZE_UI = 13.0f;      // Proportional for UI chrome
constexpr float FONT_SIZE_SMALL = 11.0f;   // Small labels
} // namespace layout

// Helper: get status badge color for a file status character
inline Color statusColor(char status) {
    switch (status) {
        case 'M': return STATUS_MODIFIED;
        case 'A': return STATUS_ADDED;
        case 'D': return STATUS_DELETED;
        case 'R': return STATUS_RENAMED;
        case 'U': case '?': return STATUS_UNTRACKED;
        case 'C': return STATUS_CONFLICT;
        default: return TEXT_SECONDARY;
    }
}

} // namespace theme
