#include <array>
#include <cmath>

#include "test_framework.h"
#include "../../src/util/review_layout.h"

using review_layout::Sidebar;
using review_layout::sidebar_width;

TEST(opening_preserves_the_resized_dock_and_review_widths) {
    for (float dock : std::array{280.f, 352.f, 480.f, 700.f}) {
        for (float panel : std::array{368.f, 920.f, 1100.f}) {
            const float width = review_layout::window_width(dock, panel, false);
            ASSERT_EQ(width, dock + panel);
            ASSERT_EQ(sidebar_width(width, dock, 200.f, Sidebar::Expanded), dock);
            ASSERT_EQ(review_layout::window_width(dock, width - dock, true), dock);
        }
    }
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
}

TEST(docked_sidebar_fills_resized_window_at_every_zoom) {
    for (float scale : std::array{1.f, 1.4f, 2.f}) {
        for (float physicalWidth : std::array{300.f, 352.f, 480.f, 700.f}) {
            const float width = physicalWidth / scale;
            ASSERT_EQ(sidebar_width(width, 280.f, 200.f, Sidebar::Collapsed), width);
        }
    }
}

TEST(opening_keeps_a_usable_review_panel) {
    ASSERT_EQ(review_layout::window_width(352.f, 0.f, false), 720.f);
    ASSERT_EQ(review_layout::window_width(352.f, 0.f, true), 352.f);
}

TEST(opening_at_zoom_preserves_the_dock_width) {
    for (float scale : std::array{1.f, 1.4f, 1.6f, 2.f}) {
        for (float dock : std::array{400.f, 480.f, 700.f}) {
            const float logicalDock = dock / scale;
            const float opened = review_layout::window_width(logicalDock, 920.f, false);
            const float sidebar = sidebar_width(opened, logicalDock, 200.f, Sidebar::Expanded);
            ASSERT_TRUE(std::abs(sidebar * scale - dock) < 0.001f);
        }
    }
    ASSERT_EQ(sidebar_width(800.f / 2.f, 280.f, 200.f, Sidebar::Expanded), 32.f);
}

int main() { RUN_ALL_TESTS(); }
