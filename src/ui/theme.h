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
inline Color BUTTON_SECONDARY = {62, 62, 64, 255};     // #3E3E40
inline Color HOVER_BG         = {50, 50, 52, 255};     // #323234
inline Color SELECTED_BG      = {36, 90, 145, 255};   // Visible blue for clear selection
inline Color FOCUS_RING       = {0, 122, 204, 255};

// Toolbar
inline Color TOOLBAR_BG       = {55, 55, 58, 255};     // #37373A (distinct from sidebar)
inline Color TOOLBAR_BTN_HOVER= {55, 55, 55, 255};     // #373737
inline Color TOOLBAR_BTN_ACTIVE={0, 122, 204, 255};     // #007ACC (blue flash on press)
inline Color TOOLBAR_BTN_DISABLED={90, 90, 90, 255};    // #5A5A5A

// Decoration badges (commit log branch/tag labels)
inline Color BADGE_BRANCH_BG  = {0, 122, 204, 255};    // #007ACC (local branch)
inline Color BADGE_HEAD_BG    = {87, 166, 74, 255};     // #57A64A (HEAD)
inline Color BADGE_REMOTE_BG  = {78, 154, 220, 255};    // #4E9ADC (remote branch)
inline Color BADGE_TAG_BG     = {85, 85, 85, 255};      // #555555 (tag)
inline Color BADGE_TAG_TEXT   = {204, 204, 204, 255};   // #CCCCCC (tag text)

// Commit graph
inline Color GRAPH_DOT        = {150, 110, 220, 255};   // Purple/violet for graph dots
inline Color GRAPH_LINE       = {80, 60, 120, 255};     // Dimmer purple for connecting line

// Row separator
inline Color ROW_SEPARATOR    = {48, 48, 48, 255};      // #303030 (more visible)

// Sidebar divider (between files and commit log sections)
inline Color SIDEBAR_DIVIDER  = {70, 70, 70, 255};      // #464646 (clearly visible)

// Empty state text (brighter than TEXT_SECONDARY for better readability)
inline Color EMPTY_STATE_TEXT = {120, 120, 120, 255};    // #787878

// Status bar
inline Color STATUS_BAR_BG    = {0, 122, 204, 255};    // Blue (#007ACC)
inline Color STATUS_BAR_TEXT  = {255, 255, 255, 255};   // White
inline Color STATUS_BAR_CLEAN = {115, 201, 145, 255};   // Green dot (#73C991)
inline Color STATUS_BAR_DIRTY = {227, 179, 65, 255};    // Yellow dot (#E3B341)
inline Color STATUS_BAR_DETACHED_BG = {204, 102, 51, 255}; // Warning orange (#CC6633)
inline Color STATUS_BAR_BTN_HOVER = {255, 255, 255, 25};  // Subtle white hover

// Section header background (used for sidebar section headers)
inline Color SECTION_HEADER_BG = {32, 32, 33, 255};     // #202021

// Selected row (solid, for file/commit rows)
inline Color SELECTED_BG_SOLID = {36, 90, 145, 255};    // Same as SELECTED_BG but solid

// Tertiary text
inline Color TEXT_TERTIARY     = {90, 90, 90, 255};     // #5A5A5A

// Layout constants
namespace layout {
constexpr int MENU_BAR_HEIGHT = 36;
constexpr int TOOLBAR_HEIGHT = 44;
constexpr int TOOLBAR_BUTTON_HEIGHT = 36;
constexpr int TOOLBAR_BUTTON_HPAD = 10;
constexpr int TOOLBAR_BUTTON_VPAD = 6;
constexpr int TOOLBAR_SEP_WIDTH = 1;
constexpr int TOOLBAR_SEP_HEIGHT = 24;
constexpr int TOOLBAR_SEP_MARGIN = 8;
constexpr int STATUS_BAR_HEIGHT = 32;
constexpr int SIDEBAR_DEFAULT_WIDTH = 300;
constexpr int SIDEBAR_MIN_WIDTH = 200;
constexpr float SIDEBAR_MIN_PCT = 0.18f;  // Min 18% of window width
constexpr int FILE_ROW_HEIGHT = 28;
constexpr int COMMIT_ROW_HEIGHT = 30;
constexpr int PADDING = 12;
constexpr int SMALL_PADDING = 6;
constexpr float FONT_SIZE_MONO = 16.0f;    // Monospace for code/diffs
constexpr float FONT_SIZE_UI = 16.0f;      // Proportional for UI chrome
constexpr float FONT_SIZE_SMALL = 13.0f;   // Small labels
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
