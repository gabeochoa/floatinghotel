#include <array>
#include <cmath>

#include "test_framework.h"
#include "../../src/util/review_layout.h"

using review_layout::Sidebar;
using review_layout::sidebar_width;

TEST(sidebar_width_stays_fixed_while_panel_opens) {
    for (float width : std::array{284.f, 400.f, 640.f, 1200.f})
        ASSERT_EQ(sidebar_width(width, 280.f, 200.f, Sidebar::Animating), 280.f);
}

TEST(expanded_sidebar_leaves_room_for_main_content) {
    ASSERT_EQ(sidebar_width(1200.f, 280.f, 200.f, Sidebar::Expanded), 280.f);
    ASSERT_EQ(sidebar_width(640.f, 280.f, 200.f, Sidebar::Expanded), 272.f);
    ASSERT_EQ(sidebar_width(400.f, 280.f, 200.f, Sidebar::Expanded), 32.f);
    ASSERT_EQ(sidebar_width(284.f, 280.f, 200.f, Sidebar::Expanded), 0.f);
    for (float width : std::array{0.f, 100.f, 284.f, 400.f, 640.f, 1200.f}) {
        const float sidebar = sidebar_width(width, 280.f, 200.f, Sidebar::Expanded);
        ASSERT_TRUE(sidebar >= 0.f);
        ASSERT_TRUE(sidebar <= width);
        if (sidebar > 0.f) ASSERT_TRUE(width - sidebar >= 368.f);
    }
}

TEST(collapsed_and_hidden_sidebar_respect_viewport) {
    ASSERT_EQ(sidebar_width(284.f, 280.f, 200.f, Sidebar::Collapsed), 284.f);
    ASSERT_EQ(sidebar_width(150.f, 280.f, 200.f, Sidebar::Collapsed), 150.f);
    ASSERT_EQ(sidebar_width(0.f, 280.f, 200.f, Sidebar::Collapsed), 0.f);
    ASSERT_EQ(sidebar_width(1200.f, 100.f, 200.f, Sidebar::Expanded), 200.f);
    ASSERT_EQ(sidebar_width(1200.f, 280.f, 200.f, Sidebar::Hidden), 0.f);
    ASSERT_EQ(sidebar_width(150.f, 280.f, 200.f, Sidebar::Animating), 150.f);
}

TEST(docked_sidebar_fills_resized_window_at_every_zoom) {
    for (float scale : std::array{1.f, 1.4f, 2.f}) {
        for (float physicalWidth : std::array{300.f, 352.f, 480.f, 700.f}) {
            const float width = physicalWidth / scale;
            ASSERT_EQ(sidebar_width(width, 280.f, 200.f, Sidebar::Collapsed), width);
        }
    }
}

TEST(resized_sidebar_stays_fixed_during_both_animation_directions) {
    for (float width : std::array{352.f, 480.f, 800.f, 1200.f})
        ASSERT_EQ(sidebar_width(width, 352.f, 200.f, Sidebar::Animating), 352.f);
}

TEST(zoomed_animation_uses_logical_viewport_width) {
    for (float scale : std::array{1.f, 1.4f, 1.6f, 2.f}) {
        for (float logicalWidth : std::array{284.f, 400.f, 640.f, 1200.f}) {
            const float physicalWidth = logicalWidth * scale;
            const float sidebar = sidebar_width(physicalWidth / scale, 280.f, 200.f, Sidebar::Animating);
            ASSERT_EQ(sidebar, 280.f);
            ASSERT_TRUE(std::abs(sidebar * scale - 280.f * scale) < 0.001f);
        }
    }
    ASSERT_EQ(sidebar_width(800.f / 2.f, 280.f, 200.f, Sidebar::Expanded), 32.f);
}

int main() { RUN_ALL_TESTS(); }
