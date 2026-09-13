#include "test_framework.h"
#include "../../src/util/document_cycle.h"

TEST(document_cycle_wraps_in_both_directions_without_reordering) {
    reading::DocumentCycle cycle{{{4}, {2}, {3}, {1}}};
    cycle.advance(1);
    ASSERT_EQ(cycle.selected()->value, 2u);
    cycle.advance(1);
    ASSERT_EQ(cycle.selected()->value, 3u);
    cycle.advance(-1);
    ASSERT_EQ(cycle.selected()->value, 2u);
    cycle.advance(-1);
    cycle.advance(-1);
    ASSERT_EQ(cycle.selected()->value, 1u);
    ASSERT_EQ(cycle.order.front().value, 4u);
    cycle.advance(1);
    ASSERT_EQ(cycle.selected()->value, 4u);
}

TEST(document_cycle_removes_closed_tabs_and_retains_selection) {
    reading::DocumentCycle cycle{{{4}, {2}, {3}, {1}}, 2};
    cycle.retain({{4}, {3}, {1}, {5}});
    ASSERT_EQ(cycle.selected()->value, 3u);
    ASSERT_EQ(cycle.order.size(), size_t{3});
    cycle.retain({{4}, {1}});
    ASSERT_EQ(cycle.selected()->value, 1u);
    cycle.retain({{4}});
    ASSERT_EQ(cycle.selected()->value, 4u);
    cycle.retain({});
    ASSERT_FALSE(cycle.selected().has_value());
    cycle.advance(1);
    cycle.advance(-1);
    ASSERT_FALSE(cycle.selected().has_value());
}

int main() { RUN_ALL_TESTS(); }
