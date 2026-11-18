#include "test_framework.h"
#include "../../src/ui/diff_metrics.h"

#include <thread>

TEST(rows_view_reuses_nearest_width_during_a_drag_and_rebuilds_on_settle) {
    ui::DiffMetricsCache cache;
    int builds = 0;
    auto build = [&] {
        ++builds;
        return std::vector<size_t>{0, 10, 20};
    };
    auto first = cache.rows_view("file-a", 100.f, 14.f, false, 'r', build);
    ASSERT_EQ(builds, 1);
    ASSERT_EQ(first.size(), size_t{3});
    // Same width: exact cache hit, no rebuild.
    cache.rows_view("file-a", 100.f, 14.f, false, 'r', build);
    ASSERT_EQ(builds, 1);
    // A lone width change after a stable period rebuilds exactly (window
    // snap, not a drag).
    std::this_thread::sleep_for(std::chrono::milliseconds(130));
    cache.rows_view("file-a", 140.f, 14.f, false, 'r', build);
    ASSERT_EQ(builds, 2);
    // Repeated changes inside 100 ms are a drag: nearest width is reused.
    auto reused = cache.rows_view("file-a", 144.f, 14.f, false, 'r', build);
    ASSERT_EQ(builds, 2);
    cache.rows_view("file-a", 148.f, 14.f, false, 'r', build);
    ASSERT_EQ(builds, 2);
    ASSERT_EQ(reused.size(), size_t{3});
    // After the width settles (~100 ms stable), the exact width is built.
    std::this_thread::sleep_for(std::chrono::milliseconds(130));
    cache.rows_view("file-a", 148.f, 14.f, false, 'r', build);
    ASSERT_EQ(builds, 3);
    // A different row set has its own track and builds independently.
    cache.rows_view("file-b", 148.f, 14.f, false, 'r', build);
    ASSERT_EQ(builds, 4);
}

TEST(wrap_widths_share_breaks_on_the_8px_grid) {
    ui::DiffMetricsCache cache;
    auto measure = [](std::string_view glyph) { return static_cast<float>(glyph.size()) * 10.f; };
    const std::string text(30, 'x');
    auto first = cache.wraps_view(text, 100.f, 14.f, false, measure, "line-1");
    const size_t scansAfterFirst = cache.wrap_scans();
    ASSERT_TRUE(scansAfterFirst > 0);
    // 103 px floors to the same 96 px grid width as 100 px: cache hit.
    auto second = cache.wraps_view(text, 103.f, 14.f, false, measure, "line-1");
    ASSERT_EQ(cache.wrap_scans(), scansAfterFirst);
    ASSERT_EQ(first.values->size(), second.values->size());
    // The next grid step (104 px) is a different width and rebuilds.
    cache.wraps_view(text, 104.f, 14.f, false, measure, "line-1");
    ASSERT_EQ(cache.wrap_scans(), scansAfterFirst + 1);
}

int main() { RUN_ALL_TESTS(); }
