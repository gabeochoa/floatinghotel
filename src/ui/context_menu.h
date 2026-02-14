#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ui {

struct ContextMenuItem {
    std::string label;
    std::string shortcutText;
    std::function<void()> action;
    bool enabled = true;
    bool isSeparator = false;
    bool isDestructive = false;

    static ContextMenuItem separator() {
        return {"", "", nullptr, true, true, false};
    }

    static ContextMenuItem item(const std::string& label,
                                std::function<void()> action,
                                bool enabled = true,
                                const std::string& shortcut = "") {
        return {label, shortcut, std::move(action), enabled, false, false};
    }

    static ContextMenuItem destructive(const std::string& label,
                                       std::function<void()> action,
                                       bool enabled = true,
                                       const std::string& shortcut = "") {
        return {label, shortcut, std::move(action), enabled, false, true};
    }
};

// Global context menu state (only one context menu open at a time)
struct ContextMenuState {
    bool isOpen = false;
    float x = 0;
    float y = 0;
    std::vector<ContextMenuItem> items;
    int hoveredIndex = -1;
};

// Show a context menu at the given position
void show_context_menu(float x, float y, std::vector<ContextMenuItem> items);

// Close the current context menu
void close_context_menu();

// Check if the context menu is currently open
bool is_context_menu_open();

// Access global state (for the render system)
ContextMenuState& get_context_menu_state();

} // namespace ui
