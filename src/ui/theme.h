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

// Status badges (WCAG AA >= 4.5:1 on dark & light surfaces)
inline Color STATUS_MODIFIED = {227, 179, 65, 255};    // Soft amber
inline Color STATUS_ADDED = {140, 209, 164, 255};      // Soft sage green (#8CD1A4)
inline Color STATUS_DELETED = {240, 138, 138, 255};    // AA coral (#F08A8A)
inline Color STATUS_RENAMED = {185, 207, 239, 255};    // Ice accent (same family as TEXT_ACCENT)
inline Color STATUS_UNTRACKED = {148, 154, 166, 255};  // AA neutral gray
inline Color STATUS_CONFLICT = {227, 158, 72, 255};    // Warm amber

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

// Disabled state (unified across all interactive elements; AA legible)
inline Color DISABLED_BG = {34, 37, 43, 255};
inline Color DISABLED_TEXT = {172, 178, 190, 255};

// Input fields
inline Color INPUT_BG = {32, 35, 41, 255};

// Interactive — one calm accent family across dark and light themes
inline Color BUTTON_PRIMARY = {193, 214, 239, 255};      // #C1D6EF (dark primary CTA)
inline Color BUTTON_PRIMARY_TEXT = {24, 34, 49, 255};    // #182231 (deep ink on dark CTA)
inline Color BUTTON_SECONDARY = {32, 35, 41, 255};
inline Color HOVER_BG = {39, 43, 51, 255};
inline Color SELECTED_BG = {43, 52, 65, 255};            // #2B3441 (calm slate selection)
inline Color SELECTED_ACCENT = {185, 207, 239, 255};     // #B9CFEF
inline Color FOCUS_RING = {130, 165, 212, 210};          // Calm slate-ice focus (never stark white)

// Toolbar
inline Color TOOLBAR_BG = {27, 29, 34, 255};
inline Color TOOLBAR_BTN_HOVER = {39, 43, 51, 255};
inline Color TOOLBAR_BTN_ACTIVE = {51, 66, 86, 255};     // Calm slate press (no electric cyan)
inline Color TOOLBAR_BTN_DISABLED = {120, 126, 136, 255};

// Decoration badges (commit log branch/tag labels)
inline Color BADGE_BRANCH_BG = {43, 58, 78, 255};        // Muted slate-blue pill
inline Color BADGE_HEAD_BG = {36, 62, 49, 255};          // Soft sage pill (AA with DIFF_ADD_TEXT)
inline Color BADGE_REMOTE_BG = {43, 52, 65, 255};
inline Color BADGE_TAG_BG = {48, 52, 60, 255};
inline Color BADGE_TAG_TEXT = {210, 214, 222, 255};

// Commit graph (calm slate, no off-system purple)
inline Color GRAPH_DOT = {156, 175, 198, 255};
inline Color GRAPH_LINE = {74, 82, 96, 255};

// Row separator & sidebar divider (single hairline token matching BORDER)
inline Color ROW_SEPARATOR = {44, 47, 54, 255};
inline Color SIDEBAR_DIVIDER = {44, 47, 54, 255};

// Empty state text (AA >= 4.5:1 on WINDOW_BG / SIDEBAR_BG)
inline Color EMPTY_STATE_TEXT = {148, 154, 166, 255};

// Status bar
inline Color STATUS_BAR_BG = {27, 29, 34, 255};
inline Color STATUS_BAR_TEXT = {156, 162, 175, 255};
inline Color STATUS_BAR_CLEAN = {140, 209, 164, 255};
inline Color STATUS_BAR_DIRTY = {227, 179, 65, 255};
inline Color STATUS_BAR_DETACHED_BG = {166, 94, 46, 255};
inline Color STATUS_BAR_BTN_HOVER = {255, 255, 255, 22};

// Section header background (used for sidebar section headers)
inline Color SECTION_HEADER_BG = {27, 29, 34, 255};

// Selected row (solid, for file/commit rows)
inline Color SELECTED_BG_SOLID = {43, 52, 65, 255};

// Tertiary text (AA >= 4.5:1 on WINDOW_BG / SIDEBAR_BG)
inline Color TEXT_TERTIARY = {146, 153, 166, 255};

// Syntax
inline Color SYNTAX_KEYWORD = {194, 168, 217, 255};
inline Color SYNTAX_STRING = {183, 205, 159, 255};
inline Color SYNTAX_NUMBER = {215, 185, 145, 255};
inline Color SYNTAX_COMMENT = {134, 145, 158, 255};

// Diff lines beyond add/delete
inline Color DIFF_MOVED_BG = {35, 55, 85, 255};
inline Color DIFF_MOVED_TEXT = {156, 195, 240, 255};
inline Color DIFF_EMPTY_BG = {24, 26, 31, 255};          // side-by-side "no line here"
inline Color FEEDBACK_LINE_BG = {45, 53, 72, 255};
inline Color HUNK_CURSOR_BG = {37, 50, 67, 255};         // #253243 calm focused hunk lift (no cobalt banner)
// Translucent reader highlights
inline Color INTRALINE_ADD_BG = {90, 210, 140, 64};
inline Color INTRALINE_DEL_BG = {235, 110, 110, 72};
inline Color FIND_MATCH_BG = {230, 180, 30, 90};
inline Color OCCURRENCE_BG = {120, 160, 230, 56};
inline Color SELECTION_BG = {90, 145, 215, 76};
inline Color CARET_GUTTER_BG = {130, 165, 212, 26};
inline Color STATUS_SUBMODULE = {146, 153, 166, 255};
inline Color SEGMENT_SELECTED_BG = {51, 55, 64, 255};

// Menu bar dropdowns and context menus
inline Color MENU_BAR_BG = {27, 29, 34, 255};
inline Color MENU_HEADER_TEXT = {176, 182, 194, 255};
inline Color MENU_ACTIVE_BG = {43, 47, 56, 255};
inline Color MENU_ACTIVE_TEXT = {236, 239, 244, 255};
inline Color MENU_PANEL_BG = {32, 35, 41, 255};
inline Color MENU_BORDER = {52, 56, 65, 255};
inline Color MENU_HOVER_BG = {43, 52, 65, 255};
inline Color MENU_TEXT = {220, 224, 232, 255};
inline Color MENU_HOVER_TEXT = {242, 245, 250, 255};
inline Color MENU_SHORTCUT_TEXT = {146, 153, 166, 255};
inline Color MENU_DISABLED_TEXT = {112, 118, 128, 255};
inline Color DESTRUCTIVE_TEXT = {240, 138, 138, 255};    // #F08A8A AA coral on dark buttons

// Repository tab strip
inline Color TAB_STRIP_BG = {27, 29, 34, 255};
inline Color TAB_ACTIVE_BG = {43, 52, 65, 255};
inline Color TAB_INACTIVE_BG = {32, 35, 41, 255};
inline Color TAB_CLOSE_HOVER = {70, 76, 88, 255};
inline Color TAB_BORDER_ACTIVE = {70, 82, 100, 255};
inline Color TAB_DOT = {130, 140, 156, 255};

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
    {&WINDOW_BG, {248, 249, 251, 255}}, {&SIDEBAR_BG, {241, 243, 246, 255}},
    {&PANEL_BG, {248, 249, 251, 255}}, {&BORDER, {220, 223, 230, 255}},
    {&TEXT_PRIMARY, {26, 30, 38, 255}}, {&TEXT_SECONDARY, {88, 95, 107, 255}},
    {&TEXT_ACCENT, {30, 78, 140, 255}}, {&SECTION_HEADER_TEXT, {94, 101, 114, 255}},
    {&TEXT_TERTIARY, {104, 109, 118, 255}},
    {&STATUS_MODIFIED, {154, 103, 0, 255}}, {&STATUS_ADDED, {31, 122, 67, 255}},
    {&STATUS_DELETED, {192, 50, 46, 255}}, {&STATUS_RENAMED, {30, 78, 140, 255}},
    {&STATUS_UNTRACKED, {104, 109, 118, 255}}, {&STATUS_CONFLICT, {180, 92, 20, 255}},
    {&DIFF_ADD_BG, {234, 246, 237, 255}}, {&DIFF_ADD_TEXT, {25, 106, 54, 255}},
    {&DIFF_DEL_BG, {252, 236, 237, 255}}, {&DIFF_DEL_TEXT, {172, 42, 40, 255}},
    {&DIFF_HUNK_HEADER, {61, 91, 128, 255}}, {&DIFF_HUNK_BG, {235, 241, 248, 255}},
    {&GUTTER_BG, {248, 249, 251, 255}}, {&GUTTER_BORDER, {226, 229, 235, 255}},
    {&GUTTER_ADD_BG, {222, 241, 227, 255}}, {&GUTTER_DEL_BG, {249, 224, 226, 255}},
    {&DISABLED_BG, {232, 234, 239, 255}}, {&DISABLED_TEXT, {90, 95, 104, 255}},
    {&INPUT_BG, {255, 255, 255, 255}},
    {&BUTTON_PRIMARY, {30, 78, 140, 255}}, {&BUTTON_PRIMARY_TEXT, {255, 255, 255, 255}},
    {&BUTTON_SECONDARY, {255, 255, 255, 255}},
    {&HOVER_BG, {232, 235, 241, 255}}, {&SELECTED_BG, {227, 236, 248, 255}},
    {&SELECTED_ACCENT, {30, 78, 140, 255}}, {&FOCUS_RING, {30, 78, 140, 195}},
    {&TOOLBAR_BG, {241, 243, 246, 255}}, {&TOOLBAR_BTN_HOVER, {228, 231, 237, 255}},
    {&TOOLBAR_BTN_ACTIVE, {212, 225, 243, 255}}, {&TOOLBAR_BTN_DISABLED, {158, 163, 172, 255}},
    {&BADGE_BRANCH_BG, {227, 236, 248, 255}}, {&BADGE_HEAD_BG, {224, 242, 230, 255}},
    {&BADGE_REMOTE_BG, {227, 236, 248, 255}}, {&BADGE_TAG_BG, {228, 231, 237, 255}},
    {&BADGE_TAG_TEXT, {58, 64, 74, 255}},
    {&GRAPH_DOT, {94, 112, 138, 255}}, {&GRAPH_LINE, {186, 194, 206, 255}},
    {&ROW_SEPARATOR, {220, 223, 230, 255}}, {&SIDEBAR_DIVIDER, {220, 223, 230, 255}},
    {&EMPTY_STATE_TEXT, {104, 109, 118, 255}},
    {&STATUS_BAR_BG, {241, 243, 246, 255}}, {&STATUS_BAR_TEXT, {88, 95, 107, 255}},
    {&STATUS_BAR_CLEAN, {31, 122, 67, 255}}, {&STATUS_BAR_DIRTY, {154, 103, 0, 255}},
    {&STATUS_BAR_DETACHED_BG, {188, 102, 44, 255}}, {&STATUS_BAR_BTN_HOVER, {0, 0, 0, 18}},
    {&SECTION_HEADER_BG, {241, 243, 246, 255}}, {&SELECTED_BG_SOLID, {227, 236, 248, 255}},
    {&SYNTAX_KEYWORD, {126, 48, 152, 255}}, {&SYNTAX_STRING, {38, 110, 48, 255}},
    {&SYNTAX_NUMBER, {158, 84, 18, 255}}, {&SYNTAX_COMMENT, {104, 112, 122, 255}},
    {&DIFF_MOVED_BG, {226, 236, 249, 255}}, {&DIFF_MOVED_TEXT, {30, 78, 140, 255}},
    {&DIFF_EMPTY_BG, {241, 243, 246, 255}}, {&FEEDBACK_LINE_BG, {232, 236, 248, 255}},
    {&HUNK_CURSOR_BG, {205, 220, 240, 255}},
    {&INTRALINE_ADD_BG, {46, 160, 67, 52}}, {&INTRALINE_DEL_BG, {207, 56, 56, 52}},
    {&FIND_MATCH_BG, {188, 140, 12, 84}}, {&OCCURRENCE_BG, {30, 78, 140, 48}},
    {&SELECTION_BG, {30, 78, 140, 68}}, {&CARET_GUTTER_BG, {30, 78, 140, 28}},
    {&STATUS_SUBMODULE, {104, 109, 118, 255}}, {&SEGMENT_SELECTED_BG, {255, 255, 255, 255}},
    {&MENU_BAR_BG, {241, 243, 246, 255}}, {&MENU_HEADER_TEXT, {52, 58, 68, 255}},
    {&MENU_ACTIVE_BG, {226, 230, 236, 255}}, {&MENU_ACTIVE_TEXT, {20, 24, 30, 255}},
    {&MENU_PANEL_BG, {255, 255, 255, 255}}, {&MENU_BORDER, {214, 218, 226, 255}},
    {&MENU_HOVER_BG, {227, 236, 248, 255}}, {&MENU_TEXT, {26, 30, 38, 255}},
    {&MENU_HOVER_TEXT, {20, 38, 66, 255}}, {&MENU_SHORTCUT_TEXT, {104, 109, 118, 255}},
    {&MENU_DISABLED_TEXT, {158, 163, 172, 255}}, {&DESTRUCTIVE_TEXT, {192, 50, 46, 255}},
    {&TAB_STRIP_BG, {241, 243, 246, 255}}, {&TAB_ACTIVE_BG, {255, 255, 255, 255}},
    {&TAB_INACTIVE_BG, {234, 237, 241, 255}}, {&TAB_CLOSE_HOVER, {214, 218, 226, 255}},
    {&TAB_BORDER_ACTIVE, {196, 206, 220, 255}}, {&TAB_DOT, {110, 118, 130, 255}},
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
constexpr float RADIUS_CONTROL = 5.0f; // Buttons, inputs, segmented pills, selected rows, keycaps
constexpr float RADIUS_BUTTON = RADIUS_CONTROL;
constexpr float RADIUS_CARD = 7.0f;    // Diff file cards, feedback cards, permission & welcome cards
constexpr float RADIUS_BOX = RADIUS_CARD;
constexpr float RADIUS_MODAL = 8.0f;   // Modals, command palette, toasts

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
        return pick({132, 178, 212, 255}, {36, 92, 148, 255});    // slate blue — C/C++
    if (ext == "md" || ext == "markdown" || ext == "txt" || ext == "rst")
        return pick({148, 184, 212, 255}, {52, 102, 148, 255});   // soft blue — docs
    if (ext == "json" || ext == "yaml" || ext == "yml" || ext == "toml" ||
        ext == "ini" || ext == "cfg" || ext == "conf" || ext == "gitignore")
        return pick({216, 188, 108, 255}, {148, 104, 16, 255});   // warm amber — config
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" ||
        ext == "ico" || ext == "svg" || ext == "webp")
        return pick({176, 152, 216, 255}, {116, 72, 168, 255});   // muted lavender — images
    if (ext == "sh" || ext == "bash" || ext == "py" || ext == "rb" ||
        ext == "js" || ext == "ts")
        return pick({140, 209, 164, 255}, {31, 122, 67, 255});    // sage green — scripts
    return TEXT_SECONDARY;
}

}  // namespace theme
