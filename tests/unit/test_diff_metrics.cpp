#include "test_framework.h"
#include "../../src/ui/diff_metrics.h"

TEST(diff_metrics_reuse_published_content_across_view_copies) {
    ecs::FileDiff file;
    file.filePath = "file.cpp";
    file.hunks.push_back({1, 1, 1, 2, "@@", {" short", "+longer line"}});
    ui::DiffMetricsCache cache;
    size_t measurements = 0;
    auto measure = [&](const std::string& text) { ++measurements; return static_cast<float>(text.size()); };
    ASSERT_EQ(cache.width(file, 14.f, false, false, measure), 12.f);
    auto copy = file;
    ASSERT_EQ(cache.width(copy, 14.f, false, false, measure), 12.f);
    ASSERT_EQ(measurements, 2u);
    ASSERT_EQ(cache.signature(file), cache.signature(copy));
    ASSERT_EQ(cache.signature_scans(), 1u);
    cache.width(copy, 15.f, false, false, measure);
    cache.width(copy, 15.f, true, false, measure);
    cache.width(copy, 15.f, true, true, measure);
    ASSERT_EQ(cache.width_scans(), 4u);
}

TEST(new_content_gets_new_metrics_even_at_the_same_path) {
    ecs::FileDiff first, second;
    first.filePath = second.filePath = "file.cpp";
    first.hunks.push_back({1, 1, 1, 1, "@@", {" first"}});
    second.hunks.push_back({1, 1, 1, 1, "@@", {" other"}});
    ui::DiffMetricsCache cache;
    ASSERT_NE(first.renderIdentity, second.renderIdentity);
    ASSERT_NE(cache.signature(first), cache.signature(second));
    ASSERT_EQ(cache.signature_scans(), 2u);
    ASSERT_TRUE(cache.bytes() <= 5 * 1024 * 1024u);
}

int main() { RUN_ALL_TESTS(); }
