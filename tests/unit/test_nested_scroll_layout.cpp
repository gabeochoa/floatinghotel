// Reproduction for the historical nested-scroll report (docs/afterhours-gaps.md,
// "Historical UI reports"): rows vanished when a panel nested in a scroll view
// resolved its content height to zero, so the app put rows straight in the
// outer scroll. Laid out against the pinned afterhours, the nested panel keeps
// its full content height and every row keeps its size and order.
#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <afterhours/ah.h>
#include <afterhours/src/plugins/autolayout.h>
#include <memory>
#include <vector>

#include "test_framework.h"

using namespace afterhours;
using namespace afterhours::ui;

namespace {
struct Tree {
    std::vector<std::unique_ptr<Entity>> entities;

    Entity& node(Size w, Size h, bool scrolls = false) {
        auto& e = *entities.emplace_back(std::make_unique<Entity>());
        auto& ui = e.addComponent<UIComponent>(e.id);
        ui.set_desired_width(w);
        ui.set_desired_height(h);
        ui.set_flex_direction(FlexDirection::Column);
        ui.set_flex_wrap(FlexWrap::NoWrap);
        if (scrolls) e.addComponent<HasScrollView>();
        return e;
    }
    static void adopt(Entity& parent, Entity& child) {
        parent.get<UIComponent>().add_child(child.id);
        child.get<UIComponent>().set_parent(parent.id);
    }
    void layout(Entity& root) {
        EntityID last = 0;
        for (auto& e : entities) last = std::max(last, e->id);
        std::vector<Entity*> mapping(static_cast<size_t>(last) + 1, nullptr);
        for (auto& e : entities) mapping[static_cast<size_t>(e->id)] = e.get();
        AutoLayout::autolayout(root.get<UIComponent>(), {1280, 720}, mapping);
    }
};

// 30 rows of 20px under `panel`; checks the panel holds all 600px and the rows
// stack in order at full height.
bool rows_survive(Tree& tree, Entity& root, Entity& panel) {
    std::vector<Entity*> rows;
    for (int i = 0; i < 30; ++i) {
        rows.push_back(&tree.node(percent(1.f), pixels(20.f)));
        Tree::adopt(panel, *rows.back());
    }
    tree.layout(root);
    if (panel.get<UIComponent>().computed[Axis::Y] < 599.f) return false;
    float previous = -1.f;
    for (auto* row : rows) {
        const auto rect = row->get<UIComponent>().rect();
        if (rect.height < 19.f || rect.width <= 0.f || rect.y <= previous) return false;
        previous = rect.y;
    }
    return true;
}
}

TEST(children_sized_panel_in_a_scroll_view_keeps_its_rows) {
    Tree tree;
    auto& scroll = tree.node(pixels(400.f), pixels(300.f), true);
    auto& panel = tree.node(percent(1.f), children());
    Tree::adopt(scroll, panel);
    ASSERT_TRUE(rows_survive(tree, scroll, panel));
}

TEST(panel_in_a_scroll_view_nested_in_another_keeps_its_rows) {
    Tree tree;
    auto& outer = tree.node(pixels(400.f), pixels(300.f), true);
    auto& header = tree.node(percent(1.f), pixels(40.f));
    auto& inner = tree.node(percent(1.f), pixels(200.f), true);
    auto& panel = tree.node(percent(1.f), children());
    Tree::adopt(outer, header);
    Tree::adopt(outer, inner);
    Tree::adopt(inner, panel);
    ASSERT_TRUE(rows_survive(tree, outer, panel));
    ASSERT_EQ(inner.get<UIComponent>().computed[Axis::Y], 200.f);
}

TEST(section_divs_stacked_in_one_scroll_each_keep_their_rows) {
    // The combined Changes page: staged and unstaged sections, children-sized,
    // one after the other in the same scroll container.
    Tree tree;
    auto& scroll = tree.node(pixels(400.f), pixels(300.f), true);
    auto& first = tree.node(percent(1.f), children());
    auto& second = tree.node(percent(1.f), children());
    Tree::adopt(scroll, first);
    Tree::adopt(scroll, second);
    ASSERT_TRUE(rows_survive(tree, scroll, first));
    ASSERT_TRUE(rows_survive(tree, scroll, second));
    ASSERT_TRUE(second.get<UIComponent>().rect().y >= first.get<UIComponent>().rect().y + 599.f);
}

int main() { RUN_ALL_TESTS(); }
