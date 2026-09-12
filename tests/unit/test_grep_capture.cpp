#include "test_framework.h"
#include "../../src/util/grep_capture.h"

static std::string record(const std::string& path, const std::string& text) {
    return path + '\0' + "12" + '\0' + text + '\n';
}

TEST(grep_capture_handles_newline_filenames_at_every_chunk_boundary) {
    auto raw = record("revision:odd\nname.cpp", "needle: content");
    for (size_t split = 0; split <= raw.size(); ++split) {
        grep_capture::Collector capture;
        ASSERT_TRUE(capture.consume(std::string_view(raw).substr(0, split)));
        ASSERT_TRUE(capture.consume(std::string_view(raw).substr(split)));
        capture.finish();
        ASSERT_EQ(capture.output(), raw);
        ASSERT_EQ(capture.matches(), 1u);
        ASSERT_FALSE(capture.truncated());
    }
}

TEST(grep_capture_caps_complete_records_without_retaining_the_next_one) {
    auto raw = record("a", "needle");
    grep_capture::Collector capture(1000, 2);
    ASSERT_FALSE(capture.consume(raw + raw + raw));
    ASSERT_EQ(capture.output(), raw + raw);
    ASSERT_EQ(capture.bytes(), raw.size() * 2);
    ASSERT_EQ(capture.matches(), 2u);
    ASSERT_TRUE(capture.truncated());
}

TEST(grep_capture_byte_caps_never_publish_partial_fields_or_lines) {
    auto first = record("a", "needle");
    auto second = record("strange\nname", "another needle");
    for (size_t extra = 0; extra < second.size(); ++extra) {
        grep_capture::Collector capture(first.size() + extra);
        ASSERT_FALSE(capture.consume(first + second));
        ASSERT_EQ(capture.output(), first);
        ASSERT_EQ(capture.bytes(), first.size() + extra);
        ASSERT_EQ(capture.matches(), 1u);
        ASSERT_TRUE(capture.truncated());
    }
}

TEST(grep_capture_eof_drops_an_unterminated_record) {
    auto first = record("a", "needle");
    grep_capture::Collector capture;
    ASSERT_TRUE(capture.consume(first + "incomplete"));
    capture.finish();
    ASSERT_EQ(capture.output(), first);
    ASSERT_TRUE(capture.truncated());
}

TEST(grep_capture_does_not_allocate_for_an_exhausted_shared_budget) {
    grep_capture::Collector capture(0);
    ASSERT_FALSE(capture.consume(record("a", "needle")));
    ASSERT_TRUE(capture.output().empty());
    ASSERT_EQ(capture.bytes(), 0u);
}

TEST(grep_capture_stops_a_giant_record_at_the_byte_limit) {
    grep_capture::Collector capture;
    ASSERT_FALSE(capture.consume(record("a", std::string(grep_capture::byteLimit * 2, 'x'))));
    ASSERT_EQ(capture.bytes(), grep_capture::byteLimit);
    ASSERT_TRUE(capture.output().empty());
    ASSERT_EQ(capture.matches(), 0u);
    ASSERT_TRUE(capture.truncated());
}

int main() { RUN_ALL_TESTS(); }
