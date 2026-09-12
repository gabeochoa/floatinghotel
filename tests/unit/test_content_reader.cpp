#include "test_framework.h"
#include "../../src/git/content_reader.h"

#include <filesystem>
#include <fstream>
#include <unistd.h>

TEST(complete_file_parser_preserves_lines_and_endings) {
    auto file = git::parse_complete_file("file.cpp", "first\r\n\nlast");
    ASSERT_TRUE(file.isFullContent);
    ASSERT_FALSE(file.isBinary);
    ASSERT_EQ(file.hunks.size(), 1u);
    ASSERT_EQ(file.hunks[0].lines, (std::vector<std::string>{" first\r", " ", " last"}));
    ASSERT_EQ(file.hunks[0].newCount, 3);
    ASSERT_TRUE(file.hunks[0].noNewline.contains(2));
    ASSERT_TRUE(git::parse_complete_file("empty", "").hunks[0].lines.empty());
}

TEST(binary_file_keeps_its_raw_content) {
    ASSERT_TRUE(git::parse_complete_file("file.bin", std::string("a\0b", 3)).isBinary);
}

TEST(file_reads_report_errors_without_throwing) {
    auto result = git::read_file({"/tmp", "fh-path-that-does-not-exist/absent", ""});
    ASSERT_FALSE(result.error.empty());
    ASSERT_TRUE(result.raw.empty());
}

TEST(async_file_read_returns_owned_content) {
    char directory[] = "/tmp/fh-content-test.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    const auto file = std::filesystem::path(path) / "source.cpp";
    { std::ofstream output(file); output << "int answer = 42;\n"; }
    auto pending = git::read_file_async({path, "source.cpp", ""});
    ASSERT_EQ(pending.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    auto result = pending.get();
    ASSERT_TRUE(result.error.empty());
    ASSERT_EQ(result.raw, "int answer = 42;\n");
    ASSERT_EQ(result.diff.hunks[0].lines[0], " int answer = 42;");
    std::filesystem::remove(file);
    std::filesystem::remove(path);
}

int main() { RUN_ALL_TESTS(); }
