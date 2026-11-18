#pragma once

// Backend-safe: only key constants, so unit tests can include this under
// AFTER_HOURS_USE_NONE. chord_pressed() lives in bindings_dispatch.h.
#include <afterhours/src/core/key_codes.h>

#include <string_view>

namespace ui::bindings {

// One binding table: the shortcut sheet is generated from it, and dispatch
// sites that match a binding exactly check it through chord_pressed()
// instead of hand-rolling modifier checks. `key` is an afterhours key code;
// 0 marks a display-only row (ranges like Cmd+1…9, dispatched by a loop).
// `command` is the app convention: Super or Control. `ctrl` requires
// Control specifically (Ctrl+G, Ctrl+Tab, where Super means something else).
struct Binding {
    const char* chord;
    const char* description;
    int key = 0;
    bool command = false;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
};

inline constexpr Binding table[] = {
    {"Cmd+Shift+/", "Open this reference", afterhours::keys::SLASH, true, false, false, true},
    {"Cmd+K", "Command palette", afterhours::keys::K, true},
    {"Cmd+P", "Find a file (paste a/ or b/ diff paths, path:line)", afterhours::keys::P, true},
    {"Cmd+R", "Go to symbol in the file", afterhours::keys::R, true},
    {"Ctrl+G", "Go to line", afterhours::keys::G, false, true},
    {"F12 / Shift+F12", "Go to definition / find references", afterhours::keys::F12},
    {"Alt+F12", "Show type information", afterhours::keys::F12, false, false, true},
    {"Ctrl+Tab", "Switch between recent documents", afterhours::keys::TAB, false, true},
    {"Ctrl+Alt+Tab", "Switch between recent repository tabs (MRU)", afterhours::keys::TAB, false, true, true},
    {"Cmd+Alt+O", "Switch repository (recent, pinned, open tabs)", afterhours::keys::O, true, false, true},
    {"Cmd+F", "Find text in the current diff", afterhours::keys::F, true},
    {"Cmd+Shift+F", "Search repository contents", afterhours::keys::F, true, false, false, true},
    {"Enter / Shift+Enter", "Next / previous find match"},
    {"Alt+Left / Alt+Right", "Back / Forward"},
    {"j or n / k", "Next / previous review hunk"},
    {"h / j / k / l", "Move the caret in a code file (Vim Mode)"},
    {"a / c", "Approve / comment on the hunk"},
    {"Cmd+C", "Copy selected code", afterhours::keys::C, true},
    {"Cmd+Enter", "Export from the feedback panel", afterhours::keys::ENTER, true},
    {"Cmd+= / Cmd+-", "Increase / decrease code text size"},
    {"Cmd+0", "Reset code text size", afterhours::keys::ZERO, true},
    {"Tab / Shift+Tab", "Move keyboard focus"},
    {"Cmd+W", "Close the current document", afterhours::keys::W, true},
    {"Cmd+Shift+T", "Reopen the last closed document", afterhours::keys::T, true, false, false, true},
    {"Cmd+T / Cmd+O", "New repository tab / open a repository"},
    {"Cmd+1 ... Cmd+9", "Switch to repository tab (Cmd+9 is the last)"},
    {"Cmd+Shift+[ / ]", "Previous / next repository tab"},
    {"Escape", "Dismiss temporary panels or dialogs", afterhours::keys::ESCAPE},
};

inline const Binding* find(std::string_view chord) {
    for (const auto& binding : table)
        if (chord == binding.chord) return &binding;
    return nullptr;
}

}  // namespace ui::bindings
