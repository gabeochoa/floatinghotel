#include "test_framework.h"
#include "../../src/util/loading_feedback.h"
using namespace std::chrono_literals;

TEST(loading_feedback_waits_until_150ms_and_disappears_on_completion) {
    loading_feedback::Delay delay;
    const auto start = loading_feedback::Delay::Clock::time_point{};
    ASSERT_FALSE(delay.visible(true, start + 1s));
    delay.restart(start);
    ASSERT_FALSE(delay.visible(true, start + 149ms));
    ASSERT_TRUE(delay.visible(true, start + 150ms));
    ASSERT_FALSE(delay.visible(false, start + 500ms));
}

TEST(replacement_requests_receive_their_own_delay) {
    loading_feedback::Delay delay;
    const auto start = loading_feedback::Delay::Clock::time_point{};
    delay.restart(start);
    ASSERT_TRUE(delay.visible(true, start + 300ms));
    delay.restart(start + 300ms);
    ASSERT_FALSE(delay.visible(true, start + 449ms));
    ASSERT_TRUE(delay.visible(true, start + 450ms));
}

int main() { RUN_ALL_TESTS(); }
