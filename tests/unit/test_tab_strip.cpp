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

int main() { RUN_ALL_TESTS(); }
