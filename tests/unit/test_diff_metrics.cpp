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

TEST(large_diff_reuses_wraps_without_raising_the_cache_budget) {
    ui::DiffMetricsCache cache;
    size_t measurements = 0;
    auto measure = [&](std::string_view glyph) { ++measurements; return static_cast<float>(glyph.size()); };
    std::vector<std::string> lines;
    for (const auto version : {"staged", "working"}) {
        for (int i = 0; i < 5000; ++i)
            lines.push_back("FH48_0123456789abcdef0123456789abcdef " + std::string(version) + " éλ row_" + std::to_string(i) + "  \r");
    }
    for (const auto& line : lines) cache.wraps(line, 1078.f, 19.36f, false, measure);
    const auto before = measurements;
    for (const auto& line : lines) cache.wraps(line, 1078.f, 19.36f, false, measure);
    ASSERT_EQ(measurements, before);
    ASSERT_EQ(cache.wrap_hits(), 10000u);
    ASSERT_TRUE(cache.bytes() <= 5 * 1024 * 1024u);
}

TEST(narrow_split_wraps_reuse_published_line_identity) {
    ui::DiffMetricsCache cache;
    size_t measurements = 0;
    auto measure = [&](std::string_view glyph) { ++measurements; return static_cast<float>(glyph.size()); };
    const std::string text = "FH48_0123456789abcdef0123456789abcdef working éλ row_1234  \r";
    for (int pass = 0; pass < 2; ++pass) {
        const auto before = measurements;
        for (int i = 0; i < 10000; ++i)
            cache.wraps(text, 8.f, 38.72f, false, measure, "100:" + std::to_string(i));
        if (pass) ASSERT_EQ(measurements, before);
    }
    const auto before = measurements;
    cache.wraps("changed", 8.f, 38.72f, false, measure, "101:0");
    ASSERT_TRUE(measurements > before);
    ASSERT_EQ(cache.wrap_hits(), 10000u);
    ASSERT_TRUE(cache.bytes() <= 5 * 1024 * 1024u);
}

TEST(three_source_pages_reuse_wraps_and_keep_key_fields_distinct) {
    ui::DiffMetricsCache cache;
    size_t measurements = 0;
    auto measure = [&](std::string_view glyph) { ++measurements; return static_cast<float>(glyph.size()); };
    const std::string text = "source_12345 éλ\tvalue\r";
    for (int pass = 0; pass < 2; ++pass) {
        const auto before = measurements;
        for (int i = 0; i < 3 * 4096; ++i)
            cache.wraps(text, 300.f, 24.64f, false, measure, "123:a:" + std::to_string(i));
        if (pass) ASSERT_EQ(measurements, before);
    }
    const auto before = cache.wrap_scans();
    cache.wraps(text, 301.f, 24.64f, false, measure, "123:a:0");
    cache.wraps(text, 300.f, 25.64f, false, measure, "123:a:0");
    cache.wraps(text, 300.f, 24.64f, true, measure, "123:a:0");
    cache.wraps(text, 300.f, 24.64f, false, measure, "123:b:0");
    cache.wraps("123:a:0", 300.f, 24.64f, false, measure);
    ASSERT_EQ(cache.wrap_scans(), before + 5);
    ASSERT_EQ(cache.wrap_hits(), 3u * 4096u);
    ASSERT_TRUE(cache.bytes() <= 5 * 1024 * 1024u);
}

TEST(source_row_extents_share_the_existing_budget_and_invalidate_with_layout) {
    ui::DiffMetricsCache cache;
    size_t builds = 0;
    auto build = [&] {
        ++builds;
        std::vector<size_t> rows{0};
        for (size_t i = 0; i < 3 * 4096; ++i) rows.push_back(rows.back() + i % 3 + 1);
        return rows;
    };
    const auto first = cache.source_rows(99, 800.f, 17.6f, false, build);
    ASSERT_EQ(first.size(), 3u * 4096u + 1u);
    ASSERT_EQ(first.back(), 3u * 4096u * 2u);
    ASSERT_EQ(cache.source_rows(99, 800.f, 17.6f, false, build), first);
    ASSERT_EQ(builds, 1u);
    cache.source_rows(100, 800.f, 17.6f, false, build);
    cache.source_rows(99, 801.f, 17.6f, false, build);
    cache.source_rows(99, 800.f, 18.6f, false, build);
    cache.source_rows(99, 800.f, 17.6f, true, build);
    ASSERT_EQ(builds, 5u);
    for (std::uint64_t i = 0; i < 100; ++i) cache.source_rows(i, 900.f, 17.6f, false, build);
    ASSERT_TRUE(cache.bytes() <= 5 * 1024 * 1024u);
    cache.source_rows(99, 800.f, 17.6f, false, build);
    ASSERT_EQ(builds, 106u);
}

TEST(shared_metric_views_remain_valid_after_eviction_and_count_pinned_bytes) {
    ui::DiffMetricsCache cache;
    auto build = [] { return std::vector<size_t>(64000, 7); };
    const auto first = cache.source_rows_view(1, 800.f, 18.f, false, build);
    const auto same = cache.source_rows_view(1, 800.f, 18.f, false, build);
    ASSERT_TRUE(first.values == same.values);
    for (std::uint64_t i = 2; i < 30; ++i) {
        auto other = cache.source_rows_view(i, 800.f, 18.f, false, build);
        ASSERT_EQ(other.back(), 7u);
        ASSERT_EQ(first.back(), 7u);
        ASSERT_TRUE(cache.bytes() <= 5 * 1024 * 1024u);
    }
    ASSERT_EQ(first.size(), 64000u);
    ASSERT_TRUE(cache.bytes() >= 64000 * sizeof(size_t));
}

TEST(shared_cache_rejects_admission_while_evicted_values_are_still_pinned) {
    SharedByteCache<std::vector<size_t>> cache(2048);
    auto retained = cache.put("held", std::vector<size_t>(150, 42), 150 * sizeof(size_t));
    ASSERT_TRUE(retained != nullptr);
    ASSERT_TRUE(cache.put("cannot-fit", std::vector<size_t>(150, 8), 150 * sizeof(size_t)) == nullptr);
    ASSERT_EQ(retained->back(), 42u);
    ASSERT_TRUE(cache.bytes() >= 150 * sizeof(size_t));
    retained.reset();
    ASSERT_TRUE(cache.put("fits-now", std::vector<size_t>(150, 8), 150 * sizeof(size_t)) != nullptr);
    ASSERT_TRUE(cache.bytes() <= 2048u);
}

TEST(cached_review_identity_preserves_keys_and_tracks_review_mutations) {
    ecs::FileDiff file;
    file.filePath = "folder/file.cpp";
    file.oldMode = "100644";
    file.newMode = "100755";
    file.hunks.push_back({1, 2, 1, 2, "@@ -1,2 +1,2 @@", {"-old", "+new", " unchanged"}});
    file.hunks.back().noNewline.insert(2);
    ui::DiffMetricsCache cache;
    ecs::ReviewComponent review;
    const auto key = ecs::ReviewComponent::hunk_key(file.filePath, file.hunks.front());
    for (int pass = 0; pass < 100; ++pass) {
        ASSERT_EQ(cache.hunk_key(file, file.hunks.front()), key);
        ASSERT_EQ(cache.reviewed(review, "wt", file), ecs::file_reviewed(review, "wt", file));
    }
    ASSERT_EQ(cache.hunk_scans(), 1u);
    review.approvedHunks.insert("wt\n" + key);
    ASSERT_FALSE(cache.reviewed(review, "wt", file));
    review.reviewedFiles["wt\n" + file.filePath] = ecs::diff_signature(file);
    ASSERT_TRUE(cache.reviewed(review, "wt", file));
    review.approvedHunks.clear();
    ASSERT_FALSE(cache.reviewed(review, "wt", file));
    auto replacement = file;
    replacement.renderIdentity = ecs::next_render_identity();
    replacement.hunks.front().lines.front() = "-changed";
    ASSERT_NE(cache.hunk_key(replacement, replacement.hunks.front()), key);
    ASSERT_FALSE(cache.reviewed(review, "wt", replacement));
    ASSERT_EQ(cache.hunk_scans(), 2u);
    auto incomplete = file;
    incomplete.isPartialContent = true;
    ASSERT_FALSE(cache.reviewed(review, "wt", incomplete));
    ASSERT_TRUE(cache.bytes() <= 5u * 1024u * 1024u);
}

int main() { RUN_ALL_TESTS(); }
