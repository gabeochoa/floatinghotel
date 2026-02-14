// Unit tests for ui::context_menu state management.
//
// The context menu module is pure state (no rendering, no Metal).

#include "test_framework.h"
#include "../../src/ui/context_menu.h"

// ===========================================================================
// State lifecycle
// ===========================================================================

TEST(context_menu_initial_state) {
    // Ensure clean state
    ui::close_context_menu();
    ASSERT_FALSE(ui::is_context_menu_open());
}

TEST(show_opens_menu) {
    ui::show_context_menu(100.0f, 200.0f, {});
    ASSERT_TRUE(ui::is_context_menu_open());
    ui::close_context_menu();
}

TEST(show_sets_position) {
    ui::show_context_menu(150.0f, 250.0f, {});
    auto& state = ui::get_context_menu_state();
    ASSERT_TRUE(state.x > 149.0f && state.x < 151.0f);
    ASSERT_TRUE(state.y > 249.0f && state.y < 251.0f);
    ui::close_context_menu();
}

TEST(show_sets_items) {
    std::vector<ui::ContextMenuItem> items;
    items.push_back(ui::ContextMenuItem::item("Copy", [] {}));
    items.push_back(ui::ContextMenuItem::item("Paste", [] {}));

    ui::show_context_menu(0.0f, 0.0f, std::move(items));
    auto& state = ui::get_context_menu_state();
    ASSERT_EQ(state.items.size(), static_cast<size_t>(2));
    ASSERT_STREQ(state.items[0].label, "Copy");
    ASSERT_STREQ(state.items[1].label, "Paste");
    ui::close_context_menu();
}

TEST(close_clears_menu) {
    ui::show_context_menu(0.0f, 0.0f,
                          {ui::ContextMenuItem::item("X", [] {})});
    ui::close_context_menu();
    ASSERT_FALSE(ui::is_context_menu_open());
    auto& state = ui::get_context_menu_state();
    ASSERT_TRUE(state.items.empty());
}

TEST(close_resets_hover_index) {
    ui::show_context_menu(0.0f, 0.0f, {});
    auto& state = ui::get_context_menu_state();
    state.hoveredIndex = 3;
    ui::close_context_menu();
    ASSERT_EQ(state.hoveredIndex, -1);
}

TEST(show_resets_hover_index) {
    auto& state = ui::get_context_menu_state();
    state.hoveredIndex = 5;
    ui::show_context_menu(0.0f, 0.0f, {});
    ASSERT_EQ(state.hoveredIndex, -1);
    ui::close_context_menu();
}

TEST(show_close_cycle) {
    for (int i = 0; i < 3; ++i) {
        ui::show_context_menu(static_cast<float>(i),
                              static_cast<float>(i), {});
        ASSERT_TRUE(ui::is_context_menu_open());
        ui::close_context_menu();
        ASSERT_FALSE(ui::is_context_menu_open());
    }
}

TEST(show_with_empty_items) {
    ui::show_context_menu(10.0f, 20.0f, {});
    auto& state = ui::get_context_menu_state();
    ASSERT_TRUE(state.isOpen);
    ASSERT_TRUE(state.items.empty());
    ui::close_context_menu();
}

// ===========================================================================
// ContextMenuItem factory methods
// ===========================================================================

TEST(separator_factory) {
    auto sep = ui::ContextMenuItem::separator();
    ASSERT_TRUE(sep.isSeparator);
    ASSERT_TRUE(sep.label.empty());
    ASSERT_TRUE(sep.shortcutText.empty());
    ASSERT_FALSE(sep.isDestructive);
    ASSERT_TRUE(sep.enabled);
}

TEST(item_factory) {
    bool called = false;
    auto item = ui::ContextMenuItem::item(
        "Edit", [&called] { called = true; }, true, "Cmd+E");
    ASSERT_STREQ(item.label, "Edit");
    ASSERT_STREQ(item.shortcutText, "Cmd+E");
    ASSERT_TRUE(item.enabled);
    ASSERT_FALSE(item.isSeparator);
    ASSERT_FALSE(item.isDestructive);
    item.action();
    ASSERT_TRUE(called);
}

TEST(item_factory_defaults) {
    auto item = ui::ContextMenuItem::item("Save", [] {});
    ASSERT_TRUE(item.enabled);
    ASSERT_TRUE(item.shortcutText.empty());
    ASSERT_FALSE(item.isSeparator);
    ASSERT_FALSE(item.isDestructive);
}

TEST(destructive_factory) {
    auto item = ui::ContextMenuItem::destructive("Delete", [] {}, true, "Del");
    ASSERT_STREQ(item.label, "Delete");
    ASSERT_TRUE(item.isDestructive);
    ASSERT_FALSE(item.isSeparator);
    ASSERT_TRUE(item.enabled);
    ASSERT_STREQ(item.shortcutText, "Del");
}

TEST(disabled_item) {
    auto item = ui::ContextMenuItem::item("Disabled", [] {}, false);
    ASSERT_FALSE(item.enabled);
}

// ===========================================================================

int main() {
    printf("=== context_menu tests ===\n");
    RUN_ALL_TESTS();
}
