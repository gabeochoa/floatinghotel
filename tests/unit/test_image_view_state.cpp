#include "test_framework.h"

#include "../../src/util/image_view_state.h"

TEST(image_view_state_labels_modes) {
    ASSERT_STREQ(image_view_state::label(image_view_state::Mode::SideBySide), "Side by side");
    ASSERT_STREQ(image_view_state::label(image_view_state::Mode::Overlay), "Overlay");
    ASSERT_STREQ(image_view_state::label(image_view_state::Mode::Wipe), "Wipe");
}

TEST(image_view_state_bounds_zoom) {
    float zoom = 1.f;
    zoom = image_view_state::zoom_in(zoom);
    ASSERT_TRUE(zoom > 1.f);
    for (int i = 0; i < 20; ++i) zoom = image_view_state::zoom_in(zoom);
    ASSERT_EQ(static_cast<int>(zoom * 100.f + 0.5f), 400);
    for (int i = 0; i < 40; ++i) zoom = image_view_state::zoom_out(zoom);
    ASSERT_EQ(static_cast<int>(zoom * 100.f + 0.5f), 25);
    ASSERT_STREQ(image_view_state::zoom_label(1.f), "100%");
}

TEST(image_view_wipe_shares_pixel_scale_and_canvas_origin) {
    auto placement = image_view_state::place(240.f, 160.f, 600.f, 300.f, 1.f);
    ASSERT_EQ(placement.scale, 1.f);
    auto before = image_view_state::wipe_crop(160.f, 240.f, false);
    auto after = image_view_state::wipe_crop(240.f, 240.f, true);
    ASSERT_EQ(before.x, 0.f);
    ASSERT_EQ(before.width, 120.f);
    ASSERT_EQ(after.x, 120.f);
    ASSERT_EQ(after.width, 120.f);
    ASSERT_EQ(image_view_state::wipe_crop(80.f, 240.f, true).width, 0.f);
    ASSERT_EQ(image_view_state::place(240.f, 160.f, 120.f, 80.f, 1.f).scale, 0.5f);
}

int main() {
    printf("=== image view state tests ===\n");
    RUN_ALL_TESTS();
}
