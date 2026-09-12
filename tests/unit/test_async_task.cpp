#include "test_framework.h"
#include "../../src/util/async_task.h"
#include <atomic>

using namespace std::chrono_literals;

TEST(foreground_has_a_reserved_worker) {
    async_work::Executor pool(2, 8);
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    auto first = async_work::launch_on(pool, [&entered, gate](std::stop_token) {
        entered.set_value(); gate.wait(); return 1;
    }, async_work::Priority::Background);
    ASSERT_EQ(entered.get_future().wait_for(2s), std::future_status::ready);
    auto second = async_work::launch_on(pool, [gate](std::stop_token) { gate.wait(); return 2; }, async_work::Priority::Background);
    auto foreground = async_work::launch_on(pool, [](std::stop_token) { return 3; });
    auto ready = foreground.wait_for(2s);
    release.set_value();
    ASSERT_EQ(ready, std::future_status::ready);
    ASSERT_EQ(foreground.get(), 3);
    ASSERT_EQ(first.get(), 1);
    ASSERT_EQ(second.get(), 2);
}

TEST(queue_is_bounded_and_foreground_overtakes_background) {
    async_work::Executor pool(1, 4);
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    auto running = async_work::launch_on(pool, [&entered, gate](std::stop_token) { entered.set_value(); gate.wait(); return 0; });
    ASSERT_EQ(entered.get_future().wait_for(2s), std::future_status::ready);
    std::vector<int> order;
    std::vector<async_work::Task<int>> pending;
    for (int i = 0; i < 3; ++i)
        pending.push_back(async_work::launch_on(pool, [&order, i](std::stop_token) { order.push_back(i); return i; }, async_work::Priority::Background));
    auto rejectedBackground = async_work::launch_on(pool, [](std::stop_token) { return 9; }, async_work::Priority::Background, -1);
    auto foreground = async_work::launch_on(pool, [&order](std::stop_token) { order.push_back(3); return 3; });
    auto rejected = async_work::launch_on(pool, [](std::stop_token) { return 9; }, async_work::Priority::Foreground, -2);
    ASSERT_EQ(rejectedBackground.get(), -1);
    ASSERT_EQ(rejected.get(), -2);
    release.set_value();
    pool.shutdown();
    ASSERT_EQ(order, (std::vector<int>{3, 0, 1, 2}));
}

TEST(shutdown_cancels_reads_and_drains_accepted_writes) {
    async_work::Executor pool(1, 4);
    std::promise<void> entered;
    auto read = async_work::launch_on(pool, [&entered](std::stop_token stop) {
        entered.set_value();
        while (!stop.stop_requested()) std::this_thread::yield();
        return 1;
    });
    ASSERT_EQ(entered.get_future().wait_for(2s), std::future_status::ready);
    auto write = async_work::launch_on(pool, [](std::stop_token stop) { return stop.stop_requested() ? -1 : 2; }, async_work::Priority::Foreground, -2, false);
    pool.shutdown();
    ASSERT_EQ(read.get(), 1);
    ASSERT_EQ(write.get(), 2);
    pool.shutdown();
    auto rejected = async_work::launch_on(pool, [](std::stop_token) { return 1; }, async_work::Priority::Foreground, -1);
    ASSERT_EQ(rejected.get(), -1);
}

int main() { RUN_ALL_TESTS(); }
