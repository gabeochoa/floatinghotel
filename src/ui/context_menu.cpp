#include "context_menu.h"

namespace ui {

static ContextMenuState g_context_menu;

void show_context_menu(float x, float y, std::vector<ContextMenuItem> items) {
    g_context_menu.isOpen = true;
    g_context_menu.x = x;
    g_context_menu.y = y;
    g_context_menu.items = std::move(items);
    g_context_menu.hoveredIndex = -1;
}

void close_context_menu() {
    g_context_menu.isOpen = false;
    g_context_menu.items.clear();
    g_context_menu.hoveredIndex = -1;
}

bool is_context_menu_open() {
    return g_context_menu.isOpen;
}

ContextMenuState& get_context_menu_state() {
    return g_context_menu;
}

} // namespace ui
