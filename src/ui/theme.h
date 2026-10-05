#pragma once

#include <afterhours/src/drawing_helpers.h>

#include <bitset>
#include <iterator>
#include <string>
#include <vector>

namespace theme {

using Color = afterhours::Color;

// Window chrome
inline Color WINDOW_BG = {21, 23, 27, 255};
inline Color SIDEBAR_BG = {27, 29, 34, 255};
inline Color PANEL_BG = {21, 23, 27, 255};
inline Color BORDER = {44, 47, 54, 255};

// Text
inline Color TEXT_PRIMARY = {228, 230, 235, 255};
inline Color TEXT_SECONDARY = {156, 162, 175, 255};
inline Color TEXT_ACCENT = {185, 207, 239, 255};
inline Color SECTION_HEADER_TEXT = {156, 162, 175, 255};

// Status badges
inline Color STATUS_MODIFIED = {227, 179, 65, 255};    // Yellow
inline Color STATUS_ADDED = {87, 166, 74, 255};        // Green
inline Color STATUS_DELETED = {220, 76, 71, 255};      // Red
inline Color STATUS_RENAMED = {78, 154, 220, 255};     // Blue
inline Color STATUS_UNTRACKED = {128, 128, 128, 255};  // Gray
inline Color STATUS_CONFLICT = {220, 140, 50, 255};    // Orange

// Diff colors
inline Color DIFF_ADD_BG = {27, 43, 37, 255};
inline Color DIFF_ADD_TEXT = {161, 214, 181, 255};
inline Color DIFF_DEL_BG = {48, 34, 37, 255};
inline Color DIFF_DEL_TEXT = {223, 156, 156, 255};
inline Color DIFF_HUNK_HEADER = {156, 175, 198, 255};
inline Color DIFF_HUNK_BG = {29, 37, 46, 255};
inline Color GUTTER_BG = {21, 23, 27, 255};
inline Color GUTTER_BORDER = {44, 47, 54, 255};
inline Color GUTTER_ADD_BG = {27, 43, 37, 255};
inline Color GUTTER_DEL_BG = {48, 34, 37, 255};

// Disabled state (unified across all interactive elements)
inline Color DISABLED_BG = {48, 48, 52, 255};        // #303034 (reads as inactive vs dark UI)
inline Color DISABLED_TEXT = {140, 140, 146, 255};   // #8C8C92 (legible label on the darker bg)

// Input fields
inline Color INPUT_BG = {32, 35, 41, 255};

// Interactive
inline Color BUTTON_PRIMARY = {62, 82, 111, 255};
inline Color BUTTON_SECONDARY = {32, 35, 41, 255};
inline Color HOVER_BG = {39, 43, 51, 255};
inline Color SELECTED_BG = {40, 48, 61, 255};
inline Color SELECTED_ACCENT = {185, 207, 239, 255};
inline Color FOCUS_RING = {185, 207, 239, 255};

// Toolbar
inline Color TOOLBAR_BG = {27, 29, 34, 255};
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
inline Color STATUS_BAR_BG = {27, 29, 34, 255};
inline Color STATUS_BAR_TEXT = {156, 162, 175, 255};
inline Color STATUS_BAR_CLEAN = {115, 201, 145, 255};  // Green dot (#73C991)
inline Color STATUS_BAR_DIRTY = {227, 179, 65, 255};   // Yellow dot (#E3B341)
inline Color STATUS_BAR_DETACHED_BG = {204, 102, 51,
                                       255};  // Warning orange (#CC6633)
inline Color STATUS_BAR_BTN_HOVER = {255, 255, 255, 25};  // Subtle white hover

// Section header background (used for sidebar section headers)
inline Color SECTION_HEADER_BG = {27, 29, 34, 255};

// Selected row (solid, for file/commit rows)
inline Color SELECTED_BG_SOLID = {40, 48, 61, 255};

// Tertiary text
inline Color TEXT_TERTIARY = {126, 133, 146, 255};

// Syntax
inline Color SYNTAX_KEYWORD = {194, 168, 217, 255};
inline Color SYNTAX_STRING = {183, 205, 159, 255};
inline Color SYNTAX_NUMBER = {215, 185, 145, 255};
inline Color SYNTAX_COMMENT = {117, 129, 142, 255};

// Diff lines beyond add/delete
inline Color DIFF_MOVED_BG = {35, 55, 85, 255};
inline Color DIFF_MOVED_TEXT = {125, 180, 255, 255};
inline Color DIFF_EMPTY_BG = {26, 26, 26, 255};  // side-by-side "no line here"
inline Color FEEDBACK_LINE_BG = {58, 68, 94, 255};
inline Color HUNK_CURSOR_BG = {38, 79, 140, 255};
// Translucent reader highlights (routed here 2026-10-04; were raw literals in diff_renderer/sidebar, invisible to the light theme)
inline Color INTRALINE_ADD_BG = {90, 230, 140, 80};
inline Color INTRALINE_DEL_BG = {240, 100, 100, 90};
inline Color FIND_MATCH_BG = {230, 180, 30, 100};
inline Color OCCURRENCE_BG = {120, 160, 230, 60};
inline Color SELECTION_BG = {58, 130, 210, 90};
inline Color CARET_GUTTER_BG = {110, 156, 220, 24};
inline Color STATUS_SUBMODULE = {170, 140, 230, 255};
inline Color SEGMENT_SELECTED_BG = {51, 55, 64, 255};

// Menu bar dropdowns and context menus
inline Color MENU_BAR_BG = {30, 30, 30, 255};
inline Color MENU_HEADER_TEXT = {170, 170, 170, 255};  // brighter than secondary so menus don't read disabled
inline Color MENU_ACTIVE_BG = {45, 45, 45, 255};
inline Color MENU_ACTIVE_TEXT = {255, 255, 255, 255};
inline Color MENU_PANEL_BG = {45, 45, 45, 255};
inline Color MENU_BORDER = {58, 58, 58, 255};
inline Color MENU_HOVER_BG = {4, 57, 94, 255};
inline Color MENU_TEXT = {204, 204, 204, 255};
inline Color MENU_HOVER_TEXT = {255, 255, 255, 255};
inline Color MENU_SHORTCUT_TEXT = {128, 128, 128, 255};
inline Color MENU_DISABLED_TEXT = {90, 90, 90, 255};
inline Color DESTRUCTIVE_TEXT = {235, 94, 94, 255};

// Repository tab strip
inline Color TAB_STRIP_BG = {27, 29, 33, 255};
inline Color TAB_ACTIVE_BG = {43, 52, 65, 255};
inline Color TAB_INACTIVE_BG = {33, 36, 42, 255};
inline Color TAB_CLOSE_HOVER = {80, 86, 98, 255};
inline Color TAB_BORDER_ACTIVE = {70, 82, 100, 255};
inline Color TAB_DOT = {110, 118, 130, 255};

// ---- Swappable themes ----
// The inline Color globals above are the LIVE palette the immediate-mode UI
// reads every frame, initialised to the dark theme. LIGHT lists a light value
// for each; set_theme() copies one side into the globals, so switching is
// instant with zero call-site churn.
struct Swatch {
    Color* live;
    Color light;
};

inline const Swatch LIGHT[] = {
    {&WINDOW_BG, {246, 246, 246, 255}}, {&SIDEBAR_BG, {236, 236, 236, 255}},
    {&PANEL_BG, {255, 255, 255, 255}}, {&BORDER, {208, 208, 212, 255}},
    {&TEXT_PRIMARY, {30, 30, 30, 255}}, {&TEXT_SECONDARY, {96, 100, 108, 255}},
    {&TEXT_ACCENT, {0, 95, 184, 255}}, {&SECTION_HEADER_TEXT, {96, 100, 108, 255}},
    {&TEXT_TERTIARY, {130, 134, 142, 255}},
    {&STATUS_MODIFIED, {166, 110, 0, 255}}, {&STATUS_ADDED, {36, 128, 50, 255}},
    {&STATUS_DELETED, {200, 50, 45, 255}}, {&STATUS_RENAMED, {30, 105, 190, 255}},
    {&STATUS_UNTRACKED, {120, 120, 120, 255}}, {&STATUS_CONFLICT, {196, 100, 20, 255}},
    {&DIFF_ADD_BG, {226, 245, 230, 255}}, {&DIFF_ADD_TEXT, {20, 110, 40, 255}},
    {&DIFF_DEL_BG, {252, 228, 230, 255}}, {&DIFF_DEL_TEXT, {176, 36, 36, 255}},
    {&DIFF_HUNK_HEADER, {50, 90, 140, 255}}, {&DIFF_HUNK_BG, {230, 238, 250, 255}},
    {&GUTTER_BG, {250, 250, 250, 255}}, {&GUTTER_BORDER, {222, 222, 226, 255}},
    {&GUTTER_ADD_BG, {208, 238, 214, 255}}, {&GUTTER_DEL_BG, {248, 212, 216, 255}},
    {&DISABLED_BG, {228, 228, 231, 255}}, {&DISABLED_TEXT, {150, 150, 156, 255}},
    {&INPUT_BG, {255, 255, 255, 255}},
    {&BUTTON_PRIMARY, {0, 110, 200, 255}}, {&BUTTON_SECONDARY, {228, 228, 231, 255}},
    {&HOVER_BG, {228, 230, 234, 255}}, {&SELECTED_BG, {205, 222, 244, 255}},
    {&SELECTED_ACCENT, {0, 95, 184, 255}}, {&FOCUS_RING, {0, 110, 200, 255}},
    {&TOOLBAR_BG, {236, 236, 236, 255}}, {&TOOLBAR_BTN_HOVER, {220, 220, 224, 255}},
    {&TOOLBAR_BTN_ACTIVE, {0, 122, 204, 255}}, {&TOOLBAR_BTN_DISABLED, {200, 200, 200, 255}},
    {&BADGE_BRANCH_BG, {0, 122, 204, 255}}, {&BADGE_HEAD_BG, {57, 150, 70, 255}},
    {&BADGE_REMOTE_BG, {60, 140, 210, 255}}, {&BADGE_TAG_BG, {210, 210, 214, 255}},
    {&BADGE_TAG_TEXT, {40, 40, 40, 255}},
    {&GRAPH_DOT, {130, 90, 200, 255}}, {&GRAPH_LINE, {170, 150, 210, 255}},
    {&ROW_SEPARATOR, {226, 226, 228, 255}}, {&SIDEBAR_DIVIDER, {205, 205, 208, 255}},
    {&EMPTY_STATE_TEXT, {130, 130, 134, 255}},
    {&STATUS_BAR_BG, {236, 236, 236, 255}}, {&STATUS_BAR_TEXT, {96, 100, 108, 255}},
    {&STATUS_BAR_CLEAN, {30, 140, 70, 255}}, {&STATUS_BAR_DIRTY, {166, 110, 0, 255}},
    {&STATUS_BAR_DETACHED_BG, {204, 102, 51, 255}}, {&STATUS_BAR_BTN_HOVER, {0, 0, 0, 20}},
    {&SECTION_HEADER_BG, {236, 236, 236, 255}}, {&SELECTED_BG_SOLID, {205, 222, 244, 255}},
    {&SYNTAX_KEYWORD, {135, 45, 165, 255}}, {&SYNTAX_STRING, {40, 115, 30, 255}},
    {&SYNTAX_NUMBER, {165, 85, 10, 255}}, {&SYNTAX_COMMENT, {112, 120, 128, 255}},
    {&DIFF_MOVED_BG, {216, 230, 250, 255}}, {&DIFF_MOVED_TEXT, {25, 95, 190, 255}},
    {&DIFF_EMPTY_BG, {240, 240, 240, 255}}, {&FEEDBACK_LINE_BG, {226, 228, 246, 255}},
    {&HUNK_CURSOR_BG, {190, 214, 244, 255}},
    {&INTRALINE_ADD_BG, {30, 140, 60, 90}}, {&INTRALINE_DEL_BG, {200, 50, 45, 90}},
    {&FIND_MATCH_BG, {180, 130, 0, 110}}, {&OCCURRENCE_BG, {0, 95, 184, 70}},
    {&SELECTION_BG, {0, 110, 200, 100}}, {&CARET_GUTTER_BG, {0, 110, 200, 36}},
    {&STATUS_SUBMODULE, {120, 70, 180, 255}}, {&SEGMENT_SELECTED_BG, {205, 222, 244, 255}},
    {&MENU_BAR_BG, {236, 236, 236, 255}}, {&MENU_HEADER_TEXT, {50, 50, 54, 255}},
    {&MENU_ACTIVE_BG, {218, 218, 222, 255}}, {&MENU_ACTIVE_TEXT, {20, 20, 20, 255}},
    {&MENU_PANEL_BG, {250, 250, 250, 255}}, {&MENU_BORDER, {205, 205, 208, 255}},
    {&MENU_HOVER_BG, {0, 110, 200, 255}}, {&MENU_TEXT, {30, 30, 30, 255}},
    {&MENU_HOVER_TEXT, {255, 255, 255, 255}}, {&MENU_SHORTCUT_TEXT, {120, 120, 124, 255}},
    {&MENU_DISABLED_TEXT, {170, 170, 174, 255}}, {&DESTRUCTIVE_TEXT, {196, 36, 36, 255}},
    {&TAB_STRIP_BG, {228, 228, 231, 255}}, {&TAB_ACTIVE_BG, {255, 255, 255, 255}},
    {&TAB_INACTIVE_BG, {236, 236, 238, 255}}, {&TAB_CLOSE_HOVER, {205, 205, 210, 255}},
    {&TAB_BORDER_ACTIVE, {0, 110, 200, 255}}, {&TAB_DOT, {150, 150, 156, 255}},
};

enum class ThemeName { Dark, Light };
inline ThemeName current_theme_name = ThemeName::Dark;

inline void set_theme(ThemeName n) {
    // The dark side is whatever the globals held before the first switch.
    static const auto dark = [] {
        std::vector<Color> values;
        for (const auto& swatch : LIGHT) values.push_back(*swatch.live);
        return values;
    }();
    current_theme_name = n;
    for (size_t i = 0; i < std::size(LIGHT); ++i)
        *LIGHT[i].live = n == ThemeName::Light ? LIGHT[i].light : dark[i];
}

// One-off colour that has no shared role in the palette.
inline Color pick(Color dark, Color light) {
    return current_theme_name == ThemeName::Light ? light : dark;
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
//   Small/Caption = 12, Medium/Body = 14, Large/Heading = 16 (XL == Large).
// All UI text uses the FontSize enum tiers, and the code-font setting
// snaps to the same tiers, so code text is on-scale too.
constexpr float FONT_CODE = 14.0f;     // Diff code text (mono), == body size

// Rounded corners (enable all four corners)
const std::bitset<4> ROUNDED_CORNERS = std::bitset<4>(0b1111);

// Corner radius in pixels. A fraction (with_roundness) is a fraction of the
// SHORT SIDE, so one constant gave a 24px-tall banner 3.6px corners and a
// 150px-tall metadata box 22.5px from the same preset. A fixed radius is what
// these actually want.
constexpr float RADIUS_BUTTON = 5.0f;  // Toolbar & dialog buttons
constexpr float RADIUS_BOX = 6.0f;     // Metadata boxes, containers, cards

// Still a fraction, and correctly so: a badge is a pill, and "half the short
// side" is the definition of one. Badges are all one text line tall, so this
// does not drift the way the box radius did.
constexpr float ROUNDNESS_BADGE = 0.6f;   // Commit/branch badge pills
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
