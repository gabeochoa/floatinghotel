#include "test_framework.h"
#include "../../src/ui/diff_metrics.h"

TEST(diff_metrics_reuse_published_content_across_view_copies) {
    ecs::FileDiff file;
    file.filePath = "file.cpp";
    file.hunks.push_back({1, 1, 1, 2, "@@", {" short", "+longer line"}});
    ui::DiffMetricsCache cache;
    size_t measurements = 0;
    auto measure = [&](std::string_view text) { ++measurements; return static_cast<float>(text.size()); };
    std::string text = "longer line";
    ASSERT_EQ(cache.wraps(text, 6.f, 14.f, false, measure), (std::vector<size_t>{0, 6, 11}));
    auto copy = file;
    auto before = measurements;
    cache.wraps(std::string(text), 6.f, 14.f, false, measure);
    ASSERT_EQ(measurements, before);
    ASSERT_EQ(cache.signature(file), cache.signature(copy));
    ASSERT_EQ(cache.signature_scans(), 1u);
    cache.wraps(text, 6.f, 15.f, false, measure);
    cache.wraps(text, 6.f, 15.f, true, measure);
    cache.wraps(text, 3.f, 15.f, true, measure);
    ASSERT_EQ(cache.wrap_scans(), 4u);
    ASSERT_EQ(cache.wrap_hits(), 1u);
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

TEST(a_bounded_source_page_reuses_all_wrap_measurements) {
    ui::DiffMetricsCache cache;
    size_t measurements = 0;
    auto measure = [&](std::string_view glyph) { ++measurements; return static_cast<float>(glyph.size()); };
    std::vector<std::string> lines;
    for (int i = 0; i < 4000; ++i) lines.push_back("int source_" + std::to_string(i) + " = 0; // a bounded source page");
    for (const auto& line : lines) cache.wraps(line, 1000.f, 17.6f, false, measure);
    auto before = measurements;
    for (const auto& line : lines) cache.wraps(line, 1000.f, 17.6f, false, measure);
    ASSERT_EQ(measurements, before);
    ASSERT_EQ(cache.wrap_hits(), 4000u);
    ASSERT_TRUE(cache.bytes() <= 5 * 1024 * 1024u);
}

int main() { RUN_ALL_TESTS(); }
