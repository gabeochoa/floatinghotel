#pragma once

#include <afterhours/src/drawing_helpers.h>

#include <bitset>
#include <string>

namespace theme {

using Color = afterhours::Color;

// Window chrome
inline Color WINDOW_BG = {30, 30, 30, 255};   // #1E1E1E
inline Color SIDEBAR_BG = {37, 37, 38, 255};  // #252526
inline Color PANEL_BG = {30, 30, 30, 255};    // #1E1E1E
inline Color BORDER = {58, 58, 58, 255};      // #3A3A3A

// Text
inline Color TEXT_PRIMARY = {204, 204, 204, 255};    // #CCCCCC
inline Color TEXT_SECONDARY = {128, 128, 128, 255};  // #808080
inline Color TEXT_ACCENT = {78, 154, 6, 255};
inline Color SECTION_HEADER_TEXT = {180, 180, 180, 255};  // uppercase group labels

// Status badges
inline Color STATUS_MODIFIED = {227, 179, 65, 255};    // Yellow
inline Color STATUS_ADDED = {87, 166, 74, 255};        // Green
inline Color STATUS_DELETED = {220, 76, 71, 255};      // Red
inline Color STATUS_RENAMED = {78, 154, 220, 255};     // Blue
inline Color STATUS_UNTRACKED = {128, 128, 128, 255};  // Gray
inline Color STATUS_CONFLICT = {220, 140, 50, 255};    // Orange

// Diff colors
inline Color DIFF_ADD_BG = {35, 52, 35, 255};         // #233423 — green tint
inline Color DIFF_ADD_TEXT = {126, 231, 135, 255};    // #7EE787
inline Color DIFF_DEL_BG = {61, 17, 23, 255};         // #3D1117
inline Color DIFF_DEL_TEXT = {255, 123, 114, 255};    // #FF7B72
inline Color DIFF_HUNK_HEADER = {78, 154, 220, 255};  // #4E9ADC
inline Color DIFF_HUNK_BG = {26, 35, 50, 255};        // #1A2332
inline Color GUTTER_BG = {30, 30, 30, 255};      // #1E1E1E (matches WINDOW_BG)
inline Color GUTTER_BORDER = {58, 58, 58, 255};  // #3A3A3A (matches BORDER)
inline Color GUTTER_ADD_BG = {13, 51, 23, 255};  // #0D3317
inline Color GUTTER_DEL_BG = {77, 17, 23, 255};  // #4D1117

// Disabled state (unified across all interactive elements)
inline Color DISABLED_BG = {48, 48, 52, 255};        // #303034 (reads as inactive vs dark UI)
inline Color DISABLED_TEXT = {140, 140, 146, 255};   // #8C8C92 (legible label on the darker bg)

// Input fields
inline Color INPUT_BG = {60, 60, 60, 255};  // #3C3C3C (VS Code input background)

// Interactive
inline Color BUTTON_PRIMARY = {0, 122, 204, 255};   // Blue
inline Color BUTTON_SECONDARY = {62, 62, 64, 255};  // #3E3E40
inline Color HOVER_BG = {42, 42, 44, 255};          // #2A2A2C (subtle)
inline Color SELECTED_BG = {36, 90, 145,
                            255};  // Visible blue for clear selection
inline Color FOCUS_RING = {0, 122, 204, 255};

// Toolbar
inline Color TOOLBAR_BG = {55, 55, 58, 255};  // #37373A (distinct from sidebar)
inline Color TOOLBAR_BTN_HOVER = {55, 55, 55, 255};  // #373737
inline Color TOOLBAR_BTN_ACTIVE = {0, 122, 204,
                                   255};  // #007ACC (blue flash on press)
inline Color TOOLBAR_BTN_DISABLED = {90, 90, 90, 255};  // #5A5A5A

// Decoration badges (commit log branch/tag labels)
inline Color BADGE_BRANCH_BG = {0, 122, 204, 255};   // #007ACC (local branch)
inline Color BADGE_HEAD_BG = {87, 166, 74, 255};     // #57A64A (HEAD)
inline Color BADGE_REMOTE_BG = {78, 154, 220, 255};  // #4E9ADC (remote branch)
inline Color BADGE_TAG_BG = {85, 85, 85, 255};       // #555555 (tag)
inline Color BADGE_TAG_TEXT = {204, 204, 204, 255};  // #CCCCCC (tag text)

// Commit graph
inline Color GRAPH_DOT = {150, 110, 220, 255};  // Purple/violet for graph dots
inline Color GRAPH_LINE = {100, 80, 150, 255}; // Connecting lines between commits

// Row separator
inline Color ROW_SEPARATOR = {48, 48, 48, 255};  // #303030 (more visible)

// Sidebar divider (between files and commit log sections)
inline Color SIDEBAR_DIVIDER = {70, 70, 70, 255};  // #464646 (clearly visible)

// Empty state text (brighter than TEXT_SECONDARY for better readability)
inline Color EMPTY_STATE_TEXT = {120, 120, 120, 255};  // #787878

// Status bar
inline Color STATUS_BAR_BG = {0, 122, 204, 255};       // Blue (#007ACC)
inline Color STATUS_BAR_TEXT = {255, 255, 255, 255};   // White
inline Color STATUS_BAR_CLEAN = {115, 201, 145, 255};  // Green dot (#73C991)
inline Color STATUS_BAR_DIRTY = {227, 179, 65, 255};   // Yellow dot (#E3B341)
inline Color STATUS_BAR_DETACHED_BG = {204, 102, 51,
                                       255};  // Warning orange (#CC6633)
inline Color STATUS_BAR_BTN_HOVER = {255, 255, 255, 25};  // Subtle white hover

// Section header background (used for sidebar section headers)
inline Color SECTION_HEADER_BG = {32, 32, 33, 255};  // #202021

// Selected row (solid, for file/commit rows)
inline Color SELECTED_BG_SOLID = {36, 90, 145,
                                  255};  // Same as SELECTED_BG but solid

// Tertiary text
inline Color TEXT_TERTIARY = {90, 90, 90, 255};  // #5A5A5A

// ---- Swappable themes ----
// The inline Color globals above are the LIVE palette the immediate-mode UI
// reads every frame. A Theme bundles a full palette; apply_theme() copies one
// into those globals, so switching is instant with zero call-site churn.
struct Theme {
    Color window_bg, sidebar_bg, panel_bg, border;
    Color text_primary, text_secondary, text_accent, text_tertiary;
    Color status_modified, status_added, status_deleted, status_renamed,
          status_untracked, status_conflict;
    Color diff_add_bg, diff_add_text, diff_del_bg, diff_del_text,
          diff_hunk_header, diff_hunk_bg;
    Color gutter_bg, gutter_border, gutter_add_bg, gutter_del_bg;
    Color disabled_bg, disabled_text, input_bg;
    Color button_primary, button_secondary, hover_bg, selected_bg, focus_ring;
    Color toolbar_bg, toolbar_btn_hover, toolbar_btn_active, toolbar_btn_disabled;
    Color badge_branch_bg, badge_head_bg, badge_remote_bg, badge_tag_bg,
          badge_tag_text;
    Color graph_dot, graph_line;
    Color row_separator, sidebar_divider, empty_state_text;
    Color status_bar_bg, status_bar_text, status_bar_clean, status_bar_dirty,
          status_bar_detached_bg, status_bar_btn_hover;
    Color section_header_bg, selected_bg_solid;
};

// Snapshot the live globals into a Theme (used to capture the default palette).
inline Theme capture_current() {
    return Theme{
        WINDOW_BG, SIDEBAR_BG, PANEL_BG, BORDER,
        TEXT_PRIMARY, TEXT_SECONDARY, TEXT_ACCENT, TEXT_TERTIARY,
        STATUS_MODIFIED, STATUS_ADDED, STATUS_DELETED, STATUS_RENAMED,
        STATUS_UNTRACKED, STATUS_CONFLICT,
        DIFF_ADD_BG, DIFF_ADD_TEXT, DIFF_DEL_BG, DIFF_DEL_TEXT,
        DIFF_HUNK_HEADER, DIFF_HUNK_BG,
        GUTTER_BG, GUTTER_BORDER, GUTTER_ADD_BG, GUTTER_DEL_BG,
        DISABLED_BG, DISABLED_TEXT, INPUT_BG,
        BUTTON_PRIMARY, BUTTON_SECONDARY, HOVER_BG, SELECTED_BG, FOCUS_RING,
        TOOLBAR_BG, TOOLBAR_BTN_HOVER, TOOLBAR_BTN_ACTIVE, TOOLBAR_BTN_DISABLED,
        BADGE_BRANCH_BG, BADGE_HEAD_BG, BADGE_REMOTE_BG, BADGE_TAG_BG,
        BADGE_TAG_TEXT,
        GRAPH_DOT, GRAPH_LINE,
        ROW_SEPARATOR, SIDEBAR_DIVIDER, EMPTY_STATE_TEXT,
        STATUS_BAR_BG, STATUS_BAR_TEXT, STATUS_BAR_CLEAN, STATUS_BAR_DIRTY,
        STATUS_BAR_DETACHED_BG, STATUS_BAR_BTN_HOVER,
        SECTION_HEADER_BG, SELECTED_BG_SOLID,
    };
}

inline void apply_theme(const Theme& t) {
    WINDOW_BG = t.window_bg; SIDEBAR_BG = t.sidebar_bg; PANEL_BG = t.panel_bg;
    BORDER = t.border;
    TEXT_PRIMARY = t.text_primary; TEXT_SECONDARY = t.text_secondary;
    TEXT_ACCENT = t.text_accent; TEXT_TERTIARY = t.text_tertiary;
    STATUS_MODIFIED = t.status_modified; STATUS_ADDED = t.status_added;
    STATUS_DELETED = t.status_deleted; STATUS_RENAMED = t.status_renamed;
    STATUS_UNTRACKED = t.status_untracked; STATUS_CONFLICT = t.status_conflict;
    DIFF_ADD_BG = t.diff_add_bg; DIFF_ADD_TEXT = t.diff_add_text;
    DIFF_DEL_BG = t.diff_del_bg; DIFF_DEL_TEXT = t.diff_del_text;
    DIFF_HUNK_HEADER = t.diff_hunk_header; DIFF_HUNK_BG = t.diff_hunk_bg;
    GUTTER_BG = t.gutter_bg; GUTTER_BORDER = t.gutter_border;
    GUTTER_ADD_BG = t.gutter_add_bg; GUTTER_DEL_BG = t.gutter_del_bg;
    DISABLED_BG = t.disabled_bg; DISABLED_TEXT = t.disabled_text;
    INPUT_BG = t.input_bg;
    BUTTON_PRIMARY = t.button_primary; BUTTON_SECONDARY = t.button_secondary;
    HOVER_BG = t.hover_bg; SELECTED_BG = t.selected_bg; FOCUS_RING = t.focus_ring;
    TOOLBAR_BG = t.toolbar_bg; TOOLBAR_BTN_HOVER = t.toolbar_btn_hover;
    TOOLBAR_BTN_ACTIVE = t.toolbar_btn_active;
    TOOLBAR_BTN_DISABLED = t.toolbar_btn_disabled;
    BADGE_BRANCH_BG = t.badge_branch_bg; BADGE_HEAD_BG = t.badge_head_bg;
    BADGE_REMOTE_BG = t.badge_remote_bg; BADGE_TAG_BG = t.badge_tag_bg;
    BADGE_TAG_TEXT = t.badge_tag_text;
    GRAPH_DOT = t.graph_dot; GRAPH_LINE = t.graph_line;
    ROW_SEPARATOR = t.row_separator; SIDEBAR_DIVIDER = t.sidebar_divider;
    EMPTY_STATE_TEXT = t.empty_state_text;
    STATUS_BAR_BG = t.status_bar_bg; STATUS_BAR_TEXT = t.status_bar_text;
    STATUS_BAR_CLEAN = t.status_bar_clean; STATUS_BAR_DIRTY = t.status_bar_dirty;
    STATUS_BAR_DETACHED_BG = t.status_bar_detached_bg;
    STATUS_BAR_BTN_HOVER = t.status_bar_btn_hover;
    SECTION_HEADER_BG = t.section_header_bg;
    SELECTED_BG_SOLID = t.selected_bg_solid;
}

// DARK = the default palette captured from the globals above (no re-listing).
inline const Theme DARK = capture_current();

inline const Theme LIGHT = {
    .window_bg = {246, 246, 246, 255}, .sidebar_bg = {236, 236, 236, 255},
    .panel_bg = {255, 255, 255, 255}, .border = {200, 200, 200, 255},
    .text_primary = {30, 30, 30, 255}, .text_secondary = {110, 110, 110, 255},
    .text_accent = {60, 120, 20, 255}, .text_tertiary = {150, 150, 150, 255},
    .status_modified = {180, 120, 0, 255}, .status_added = {40, 130, 50, 255},
    .status_deleted = {200, 50, 45, 255}, .status_renamed = {40, 110, 190, 255},
    .status_untracked = {130, 130, 130, 255}, .status_conflict = {200, 110, 30, 255},
    .diff_add_bg = {225, 245, 225, 255}, .diff_add_text = {20, 110, 40, 255},
    .diff_del_bg = {250, 225, 228, 255}, .diff_del_text = {180, 40, 40, 255},
    .diff_hunk_header = {40, 110, 190, 255}, .diff_hunk_bg = {225, 235, 250, 255},
    .gutter_bg = {246, 246, 246, 255}, .gutter_border = {200, 200, 200, 255},
    .gutter_add_bg = {210, 240, 215, 255}, .gutter_del_bg = {250, 215, 218, 255},
    .disabled_bg = {225, 225, 228, 255}, .disabled_text = {160, 160, 165, 255},
    .input_bg = {255, 255, 255, 255},
    .button_primary = {0, 122, 204, 255}, .button_secondary = {225, 225, 228, 255},
    .hover_bg = {230, 230, 232, 255}, .selected_bg = {180, 205, 235, 255},
    .focus_ring = {0, 122, 204, 255},
    .toolbar_bg = {230, 230, 232, 255}, .toolbar_btn_hover = {220, 220, 222, 255},
    .toolbar_btn_active = {0, 122, 204, 255}, .toolbar_btn_disabled = {200, 200, 200, 255},
    .badge_branch_bg = {0, 122, 204, 255}, .badge_head_bg = {57, 166, 74, 255},
    .badge_remote_bg = {78, 154, 220, 255}, .badge_tag_bg = {200, 200, 200, 255},
    .badge_tag_text = {40, 40, 40, 255},
    .graph_dot = {130, 90, 200, 255}, .graph_line = {170, 150, 200, 255},
    .row_separator = {220, 220, 220, 255}, .sidebar_divider = {200, 200, 200, 255},
    .empty_state_text = {150, 150, 150, 255},
    .status_bar_bg = {0, 122, 204, 255}, .status_bar_text = {255, 255, 255, 255},
    .status_bar_clean = {30, 150, 70, 255}, .status_bar_dirty = {180, 130, 0, 255},
    .status_bar_detached_bg = {204, 102, 51, 255}, .status_bar_btn_hover = {0, 0, 0, 25},
    .section_header_bg = {235, 235, 236, 255}, .selected_bg_solid = {180, 205, 235, 255},
};

enum class ThemeName { Dark, Light };
inline ThemeName current_theme_name = ThemeName::Dark;

inline void set_theme(ThemeName n) {
    current_theme_name = n;
    apply_theme(n == ThemeName::Light ? LIGHT : DARK);
}

inline void cycle_theme() {
    set_theme(current_theme_name == ThemeName::Dark ? ThemeName::Light
                                                    : ThemeName::Dark);
}

// Layout constants
namespace layout {
constexpr int MENU_BAR_HEIGHT = 24;
constexpr int TOOLBAR_HEIGHT = 44;
constexpr int TOOLBAR_BUTTON_HEIGHT = 36;
constexpr int TOOLBAR_BUTTON_HPAD = 10;
constexpr int TOOLBAR_BUTTON_VPAD = 6;
constexpr int TOOLBAR_SEP_WIDTH = 1;
constexpr int TOOLBAR_SEP_HEIGHT = 24;
constexpr int TOOLBAR_SEP_MARGIN = 8;
constexpr int STATUS_BAR_HEIGHT = 22;
constexpr int SIDEBAR_DEFAULT_WIDTH = 300;
constexpr int SIDEBAR_MIN_WIDTH = 200;
constexpr float SIDEBAR_MIN_PCT = 0.18f;  // Min 18% of window width
constexpr int ROW_HEIGHT = 24;          // unified list-row height
constexpr int FILE_ROW_HEIGHT = ROW_HEIGHT;
constexpr int COMMIT_ROW_HEIGHT = ROW_HEIGHT;
constexpr int SECTION_HEADER_HEIGHT = 22;
constexpr int ICON_LG = 32;             // large decorative empty-state glyph
constexpr int PADDING = 12;
constexpr int SMALL_PADDING = 6;

// Spacing scale (h720 reference px). Prefer these over ad-hoc literals so gaps
// stay on a consistent 4px rhythm.
constexpr int SPACE_1 = 4;
constexpr int SPACE_2 = 8;
constexpr int SPACE_3 = 12;
constexpr int SPACE_4 = 16;
constexpr int SPACE_6 = 24;
constexpr float BORDER_WIDTH = 1.0f;    // hairline border width (h720)
// Typography: 3 tiers only, set in preload.cpp as FontSize:
//   Small/Caption = 12, Medium/Body = 14, Large/Heading = 18 (XL == Large).
// All UI text uses the FontSize enum tiers. The one exception is monospace
// diff code, which needs a fixed size decoupled from the UI tiers:
constexpr float FONT_CODE = 14.0f;     // Diff code text (mono), == body size

// Rounded corners (enable all four corners)
const std::bitset<4> ROUNDED_CORNERS = std::bitset<4>(0b1111);

// Roundness (0.0 = square, 1.0 = fully round/pill)
constexpr float ROUNDNESS_BUTTON = 0.4f;  // Toolbar & dialog buttons
constexpr float ROUNDNESS_BADGE = 0.6f;   // Commit/branch badge pills
constexpr float ROUNDNESS_BOX = 0.3f;     // Metadata boxes, containers
}  // namespace layout

// Helper: get status badge color for a file status character
inline Color statusColor(char status) {
    switch (status) {
        case 'M':
            return STATUS_MODIFIED;
        case 'A':
            return STATUS_ADDED;
        case 'D':
            return STATUS_DELETED;
        case 'R':
            return STATUS_RENAMED;
        case 'U':
        case '?':
            return STATUS_UNTRACKED;
        case 'C':
            return STATUS_CONFLICT;
        default:
            return TEXT_SECONDARY;
    }
}

// Helper: colored "type dot" for a file, keyed off its extension. Used by the
// sidebar file list and the commit-detail file summary. Colored badges (not
// glyphs/atlas) per the icon-strategy decision.
inline Color fileTypeColor(const std::string& path) {
    auto slash = path.find_last_of('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    auto dot = name.find_last_of('.');
    std::string ext = (dot == std::string::npos || dot == 0) ? "" : name.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>((c >= 'A' && c <= 'Z') ? c + 32 : c);

    if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "h" ||
        ext == "hpp" || ext == "c")
        return Color{81, 154, 186, 255};    // blue — C/C++
    if (ext == "md" || ext == "markdown" || ext == "txt" || ext == "rst")
        return Color{120, 170, 200, 255};   // light blue — docs
    if (ext == "json" || ext == "yaml" || ext == "yml" || ext == "toml" ||
        ext == "ini" || ext == "cfg" || ext == "conf" || ext == "gitignore")
        return Color{203, 203, 65, 255};    // yellow — config
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" ||
        ext == "ico" || ext == "svg" || ext == "webp")
        return Color{160, 116, 196, 255};   // purple — images
    if (ext == "sh" || ext == "bash" || ext == "py" || ext == "rb" ||
        ext == "js" || ext == "ts")
        return Color{115, 201, 145, 255};   // green — scripts
    return Color{109, 128, 134, 255};       // neutral gray
}

}  // namespace theme
