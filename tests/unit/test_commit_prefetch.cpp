#include "test_framework.h"
#include "../../src/util/commit_prefetch.h"
using namespace std::chrono_literals;

TEST(prefetch_requires_a_stable_candidate_and_restarts_for_context_changes) {
    commit_prefetch::State state;
    auto now = loading_feedback::Delay::Clock::time_point{};
    commit_prefetch::Candidate candidate{"repo", "commit", 3, false};
    ASSERT_FALSE(state.observe(candidate, now));
    ASSERT_FALSE(state.observe(candidate, now + 149ms));
    ASSERT_TRUE(state.observe(candidate, now + 150ms));
    candidate.context = 20;
    ASSERT_FALSE(state.observe(candidate, now + 151ms));
    ASSERT_FALSE(state.observe(candidate, now + 300ms));
    ASSERT_TRUE(state.observe(candidate, now + 301ms));
    state.attempted = true;
    ASSERT_FALSE(state.observe(candidate, now + 1s));
    ASSERT_FALSE(state.observe({}, now + 2s));
}

TEST(rapid_hover_cannot_queue_another_prefetch_until_cancellation_settles) {
    commit_prefetch::State state;
    auto now = loading_feedback::Delay::Clock::time_point{};
    commit_prefetch::Candidate first{"repo", "first"};
    state.observe(first, now);
    ASSERT_TRUE(state.observe(first, now + 150ms));
    std::promise<bool> result;
    std::stop_source stop;
    state.future = async_work::Task<bool>(result.get_future(), stop);
    state.attempted = true;
    for (int i = 0; i < 100; ++i) {
        commit_prefetch::Candidate next{"repo", std::to_string(i)};
        ASSERT_FALSE(state.observe(next, now + 1s));
        ASSERT_FALSE(state.observe(next, now + 2s));
    }
    ASSERT_TRUE(stop.stop_requested());
    ASSERT_EQ(state.cancelled, 1u);
    result.set_value(false);
    ASSERT_TRUE(state.observe(state.candidate, now + 3s));
    ASSERT_FALSE(state.future.valid());
    ASSERT_EQ(state.discarded, 1u);
}

TEST(prefetch_completion_retains_no_patch_payload_and_errors_allow_future_candidates) {
    commit_prefetch::State state;
    auto now = loading_feedback::Delay::Clock::time_point{};
    commit_prefetch::Candidate first{"repo", "first"};
    state.observe(first, now);
    std::promise<bool> result;
    state.future = async_work::Task<bool>(result.get_future(), std::stop_source{});
    state.attempted = true;
    result.set_value(true);
    ASSERT_FALSE(state.observe(first, now + 1s));
    ASSERT_EQ(state.completed, 1u);
    ASSERT_FALSE(state.future.valid());
    first.repository = "another repo";
    ASSERT_FALSE(state.observe(first, now + 2s));
    ASSERT_TRUE(state.observe(first, now + 3s));
}

int main() { RUN_ALL_TESTS(); }
