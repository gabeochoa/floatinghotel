#include "test_framework.h"
#include "../../src/util/file_page_stats.h"

TEST(file_page_stats_counts_raw_decoded_and_display_line_bytes) {
    ecs::FileDiff file;
    ecs::DiffHunk hunk;
    hunk.lines = {" alpha", " beta"};
    file.hunks.push_back(hunk);
    auto stats = file_page::stats("alpha\nbeta\n", {file}, "alpha\nbeta\n");
    ASSERT_EQ(stats.rawBytes, 11u);
    ASSERT_EQ(stats.decodedBytes, 11u);
    ASSERT_EQ(stats.lineBytes, 11u);
    ASSERT_EQ(stats.lines, 2u);
    ASSERT_TRUE(stats.bounded());
}

TEST(file_page_stats_bounds_each_representation_independently) {
    file_page::Stats stats{file_page::byteLimit, 3 * file_page::byteLimit,
        3 * file_page::byteLimit + static_cast<size_t>(file_page::lineLimit),
        static_cast<size_t>(file_page::lineLimit)};
    ASSERT_TRUE(stats.bounded());
    ++stats.rawBytes;
    ASSERT_FALSE(stats.bounded());
    --stats.rawBytes;
    ++stats.decodedBytes;
    ASSERT_FALSE(stats.bounded());
    --stats.decodedBytes;
    ++stats.lineBytes;
    ASSERT_FALSE(stats.bounded());
    --stats.lineBytes;
    ++stats.lines;
    ASSERT_FALSE(stats.bounded());
}

int main() { RUN_ALL_TESTS(); }
