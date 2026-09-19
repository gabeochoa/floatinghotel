#include "context_menu.h"

namespace ui {

static ContextMenuState g_context_menu;
static std::function<std::string()> g_owner_provider;

void set_menu_owner_provider(std::function<std::string()> provider) {
    g_owner_provider = std::move(provider);
}

std::string current_menu_owner() {
    return g_owner_provider ? g_owner_provider() : std::string{};
}

void show_context_menu(float x, float y, std::vector<ContextMenuItem> items) {
    g_context_menu.owner = current_menu_owner();
    for (auto& item : items) {
        if (!item.action) continue;
        item.action = [owner = g_context_menu.owner, action = std::move(item.action)] {
            if (owner == current_menu_owner()) action();
        };
    }
    g_context_menu.isOpen = true;
    g_context_menu.x = x;
    g_context_menu.y = y;
    g_context_menu.items = std::move(items);
    g_context_menu.hoveredIndex = -1;
    g_context_menu.scrollOffset = 0.f;
}

void close_context_menu() {
    g_context_menu.isOpen = false;
    g_context_menu.items.clear();
    g_context_menu.hoveredIndex = -1;
    g_context_menu.scrollOffset = 0.f;
}

bool is_context_menu_open() {
    if (g_context_menu.isOpen && g_context_menu.owner != current_menu_owner()) close_context_menu();
    return g_context_menu.isOpen;
}

ContextMenuState& get_context_menu_state() {
    is_context_menu_open();
    return g_context_menu;
}

} // namespace ui
