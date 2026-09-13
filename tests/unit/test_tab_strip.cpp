#include "test_framework.h"
#include "../../src/util/tab_strip.h"

TEST(tab_widths_keep_short_titles_readable_and_bound_long_titles) {
    auto widths = reading::tab_widths({40.f, 220.f, 2000.f}, 500.f);
    ASSERT_EQ(widths[0], 160.f);
    ASSERT_EQ(widths[1], 220.f);
    ASSERT_EQ(widths[2], 360.f);
}

TEST(narrow_viewport_still_contains_the_entire_active_tab) {
    auto widths = reading::tab_widths({40.f, 2000.f}, 90.f);
    ASSERT_EQ(widths[0], 90.f);
    ASSERT_EQ(widths[1], 90.f);
    ASSERT_EQ(reading::reveal_tab(0, 90, 90, 90, 180), 90.f);
}

TEST(reveal_moves_only_when_the_active_tab_is_outside_the_viewport) {
    ASSERT_EQ(reading::reveal_tab(100, 150, 160, 400, 1000), 100.f);
    ASSERT_EQ(reading::reveal_tab(200, 150, 160, 400, 1000), 150.f);
    ASSERT_EQ(reading::reveal_tab(100, 700, 160, 400, 1000), 460.f);
}

TEST(reveal_clamps_after_removal_or_expansion) {
    ASSERT_EQ(reading::reveal_tab(700, 600, 160, 500, 760), 260.f);
    ASSERT_EQ(reading::reveal_tab(700, 0, 160, 1000, 760), 0.f);
}

TEST(tab_insertion_uses_midpoints_and_supports_both_ends) {
    const std::vector<float> widths{160, 240, 200};
    ASSERT_EQ(reading::tab_insertion(widths, -10), size_t{0});
    ASSERT_EQ(reading::tab_insertion(widths, 79), size_t{0});
    ASSERT_EQ(reading::tab_insertion(widths, 80), size_t{1});
    ASSERT_EQ(reading::tab_insertion(widths, 279), size_t{1});
    ASSERT_EQ(reading::tab_insertion(widths, 280), size_t{2});
    ASSERT_EQ(reading::tab_insertion(widths, 900), size_t{3});
    ASSERT_EQ(reading::tab_insertion({}, 30), size_t{0});
}

TEST(tab_edge_scroll_is_bounded_and_only_near_edges) {
    ASSERT_EQ(reading::tab_edge_scroll(200, 400, .02f), 0.f);
    ASSERT_EQ(reading::tab_edge_scroll(12, 400, .02f), -4.f);
    ASSERT_EQ(reading::tab_edge_scroll(388, 400, .02f), 4.f);
    ASSERT_EQ(reading::tab_edge_scroll(400, 400, 10.f), 20.f);
    ASSERT_EQ(reading::tab_edge_scroll(0, 400, -1.f), 0.f);
    ASSERT_EQ(reading::tab_edge_scroll(0, 0, .02f), 0.f);
}

int main() { RUN_ALL_TESTS(); }
