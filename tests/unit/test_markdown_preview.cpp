#include "test_framework.h"

#include "../../src/util/markdown_preview.h"

TEST(markdown_preview_recognizes_markdown_paths) {
    ASSERT_TRUE(markdown_preview::is_markdown_path("README.md"));
    ASSERT_TRUE(markdown_preview::is_markdown_path("notes.markdown"));
    ASSERT_FALSE(markdown_preview::is_markdown_path("notes.txt"));
}

TEST(markdown_preview_parses_safe_blocks) {
    auto blocks = markdown_preview::parse("# Title\n- item\n![alt](https://example.invalid/a.png)\n<script>x</script>\n```\ncode\n```\n");
    ASSERT_EQ(blocks[0].kind, markdown_preview::Kind::Heading);
    ASSERT_STREQ(blocks[0].text, "Title");
    ASSERT_EQ(blocks[1].kind, markdown_preview::Kind::ListItem);
    ASSERT_STREQ(blocks[1].text, "• item");
    ASSERT_EQ(blocks[2].kind, markdown_preview::Kind::Image);
    ASSERT_STREQ(blocks[2].text, "Image omitted: alt");
    ASSERT_EQ(blocks[3].kind, markdown_preview::Kind::Paragraph);
    ASSERT_STREQ(blocks[3].text, "<script>x</script>");
    ASSERT_EQ(blocks[4].kind, markdown_preview::Kind::Code);
    ASSERT_STREQ(blocks[4].text, "code");
}

TEST(markdown_preview_caches_parsing_and_reflows_measured_lines) {
    markdown_preview::Cache cache;
    int measurements = 0;
    auto measure = [&](const std::string& text, markdown_preview::Kind, float) {
        ++measurements;
        return static_cast<float>(text.size());
    };
    ASSERT_TRUE(markdown_preview::update(cache, "first", "# Title\nabcdefghij", 5.f, 1.f, 14.f, 4.f, measure));
    ASSERT_EQ(cache.lines.size(), 3u);
    ASSERT_EQ(cache.lines[1].text, "abcde");
    ASSERT_EQ(cache.lines[2].text, "fghij");
    const auto* parsed = cache.blocks.data();
    measurements = 0;
    ASSERT_FALSE(markdown_preview::update(cache, "first", "# Title\nabcdefghij", 5.f, 1.f, 14.f, 4.f, measure));
    ASSERT_EQ(measurements, 0);
    ASSERT_TRUE(markdown_preview::update(cache, "first", "# Title\nabcdefghij", 10.f, 1.f, 14.f, 4.f, measure));
    ASSERT_EQ(cache.blocks.data(), parsed);
    ASSERT_EQ(cache.lines.size(), 2u);
    ASSERT_EQ(cache.offsets.size(), cache.lines.size() + 1);
    for (float offset : cache.offsets) ASSERT_EQ(std::fmod(offset, 4.f), 0.f);
    ASSERT_EQ(markdown_preview::visible_rows(cache, 10000.f, 100.f), (std::pair<size_t, size_t>{2, 2}));
    ASSERT_TRUE(markdown_preview::update(cache, "second", "replacement", 20.f, 1.f, 14.f, 4.f, measure));
    ASSERT_EQ(cache.lines.front().text, "replacement");
}

TEST(markdown_preview_wraps_words_and_splits_only_oversized_tokens) {
    auto measure = [](const std::string& text) { return static_cast<float>(text.size()); };
    ASSERT_EQ(markdown_preview::wrap_paragraph("words fit here", 9.f, measure), (std::vector<std::string>{"words fit", "here"}));
    ASSERT_EQ(markdown_preview::wrap_paragraph("abcdefghijk word", 5.f, measure), (std::vector<std::string>{"abcde", "fghij", "k", "word"}));
}

TEST(markdown_preview_retains_source_positions_through_fences_and_wrapping) {
    markdown_preview::Cache cache;
    auto measure = [](const std::string& text, markdown_preview::Kind, float) {
        return static_cast<float>(reading::column_at_byte(text, text.size()) - 1);
    };
    markdown_preview::update(cache, "positions", "  # Title\n```cpp\n  café  code\n```\n  one   two three", 5.f, 1.f, 14.f, 1.f, measure);
    ASSERT_EQ(cache.lines[0].sourceLine, 1);
    ASSERT_EQ(cache.lines[0].sourceColumn, 5);
    ASSERT_EQ(cache.lines[1].text, "  caf");
    ASSERT_EQ(cache.lines[1].sourceLine, 3);
    ASSERT_EQ(cache.lines[2].text, "é  co");
    ASSERT_EQ(cache.lines[2].sourceColumn, 6);
    ASSERT_EQ(cache.lines[4].text, "one");
    ASSERT_EQ(cache.lines[4].sourceLine, 5);
    ASSERT_EQ(cache.lines[4].sourceColumn, 3);
    ASSERT_EQ(cache.lines[5].sourceColumn, 9);
    ASSERT_EQ(cache.lines[6].sourceColumn, 13);
}

int main() {
    printf("=== markdown preview tests ===\n");
    RUN_ALL_TESTS();
}
