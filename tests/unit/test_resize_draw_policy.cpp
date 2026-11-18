#include "test_framework.h"
#include "../../src/platform/resize_draw_policy.h"

TEST(resize_draw_throttles_to_one_per_frame_and_never_during_a_draw) {
    ASSERT_TRUE(resize_draw::should_draw(1.000, 0.0, false));
    ASSERT_FALSE(resize_draw::should_draw(1.004, 1.000, false));
    ASSERT_TRUE(resize_draw::should_draw(1.008, 1.000, false));
    ASSERT_FALSE(resize_draw::should_draw(2.000, 1.000, true));
}

int main() { RUN_ALL_TESTS(); }
