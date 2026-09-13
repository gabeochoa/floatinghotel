#include "test_framework.h"
#include "../../src/git/source_find.h"
#include "../../src/git/git_runner.h"
#include "../../src/util/file_page.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>

struct FindFixture {
    std::filesystem::path path;
    FindFixture() {
        char pattern[] = "/tmp/fh-find.XXXXXX";
        path = mkdtemp(pattern);
    }
    ~FindFixture() { std::filesystem::remove_all(path); }
    void write(const std::string& bytes) { std::ofstream out(path / "a.txt", std::ios::binary); out << bytes; }
    git::FileRequest request(std::string revision = "", std::string encoding = "auto") const {
        return {path.string(), "a.txt", std::move(revision), {}, std::move(encoding)};
    }
};

TEST(find_scans_beyond_the_first_page_with_decoded_columns) {
    FindFixture fixture;
    std::string text;
    for (int i = 1; i < 5000; ++i) text += "ordinary line\r\n";
    text += "éλ NEEDLE final";
    fixture.write(text);
    auto result = git::find_source(fixture.request(), "NEEDLE");
    ASSERT_TRUE(result.error.empty()); ASSERT_EQ(result.matches.size(), 1u);
    ASSERT_EQ(result.matches[0].line, 5000); ASSERT_EQ(result.matches[0].column, 4);
    ASSERT_EQ(result.scannedBytes, text.size()); ASSERT_TRUE(result.maxPageBytes <= file_page::byteLimit);
    auto request = fixture.request();
    request.page = {ecs::FilePageRequest::Action::TargetLine, {}, result.matches[0].line, result.sourceIdentity, 0, result.matches[0].column};
    auto page = git::read_file(request);
    ASSERT_EQ(page.diff.hunks[0].lines[5000 - page.page.begin.line], " éλ NEEDLE final");
}

TEST(find_preserves_matches_and_columns_across_long_line_fragments) {
    FindFixture fixture;
    auto text = std::string(file_page::byteLimit - 3, ' ') + "NEEDLE" + std::string(40, ' ') + "é NEEDLE";
    fixture.write(text);
    auto result = git::find_source(fixture.request(), "NEEDLE");
    ASSERT_TRUE(result.error.empty()); ASSERT_EQ(result.matches.size(), 2u);
    ASSERT_EQ(result.matches[0].column, static_cast<int>(file_page::byteLimit - 2));
    ASSERT_EQ(result.matches[1].column, static_cast<int>(file_page::byteLimit + 46));
    fixture.write(std::string(file_page::byteLimit - 3, ' ') + "aaaaaa");
    auto repeated = git::find_source(fixture.request(), "aa");
    ASSERT_EQ(repeated.matches.size(), 3u);
    ASSERT_EQ(repeated.matches[1].column - repeated.matches[0].column, 2);
    ASSERT_EQ(repeated.matches[2].column - repeated.matches[1].column, 2);
}

TEST(find_decodes_utf16_and_keeps_bom_out_of_columns) {
    FindFixture fixture;
    std::string text = "\xff\xfe";
    auto append = [&](std::uint16_t c) { text.push_back(static_cast<char>(c & 255)); text.push_back(static_cast<char>(c >> 8)); };
    for (size_t i = 0; i < file_page::byteLimit / 2; ++i) append(' ');
    append(0xd83d); append(0xde42); append(' ');
    for (char c : std::string("NEEDLE")) append(c);
    fixture.write(text);
    auto result = git::find_source(fixture.request(), "NEEDLE");
    ASSERT_TRUE(result.error.empty()); ASSERT_EQ(result.matches.size(), 1u);
    ASSERT_EQ(result.matches[0].column, static_cast<int>(file_page::byteLimit / 2 + 3));
    auto request = fixture.request();
    request.page = {ecs::FilePageRequest::Action::TargetLine, {}, result.matches[0].line, result.sourceIdentity, 0, result.matches[0].column};
    request.detectedEncoding = "utf16le";
    ASSERT_TRUE(git::read_file(request).diff.hunks[0].lines[0].ends_with("🙂 NEEDLE"));
}

TEST(find_caps_results_and_reports_only_an_actual_limit) {
    FindFixture fixture;
    std::string text;
    for (int i = 0; i < 5000; ++i) text += "needle\n";
    fixture.write(text);
    auto exact = git::find_source(fixture.request(), "needle");
    ASSERT_EQ(exact.matches.size(), 5000u); ASSERT_FALSE(exact.limited);
    fixture.write(text + "needle");
    auto capped = git::find_source(fixture.request(), "needle");
    ASSERT_EQ(capped.matches.size(), 5000u); ASSERT_TRUE(capped.limited);
    ASSERT_TRUE(capped.maxPageBytes <= file_page::byteLimit);
}

TEST(find_reads_historical_and_index_files_missing_from_the_checkout) {
    FindFixture fixture;
    auto run = [&](std::vector<std::string> args) { auto result = git::git_run(fixture.path.string(), args); ASSERT_TRUE(result.success()); return result.stdout_str(); };
    run({"init", "-q", "-b", "main"}); run({"config", "user.name", "Find fixture"});
    run({"config", "user.email", "find@example.invalid"}); run({"config", "commit.gpgsign", "false"});
    fixture.write("old NEEDLE\n"); run({"add", "."}); run({"commit", "-qm", "Original"});
    auto oid = run({"rev-parse", "HEAD"}); oid.pop_back();
    std::filesystem::remove(fixture.path / "a.txt");
    for (const auto& revision : {oid, std::string("INDEX")}) {
        auto result = git::find_source(fixture.request(revision), "NEEDLE");
        ASSERT_TRUE(result.error.empty()); ASSERT_EQ(result.matches.size(), 1u); ASSERT_EQ(result.matches[0].column, 5);
    }
    ASSERT_FALSE(git::find_source(fixture.request(), "NEEDLE").error.empty());
    ASSERT_FALSE(git::find_source(fixture.request(std::string(40, 'f')), "NEEDLE").error.empty());
}

TEST(find_cancels_and_reports_binary_and_query_limits) {
    FindFixture fixture;
    fixture.write(std::string("a\0b", 3));
    ASSERT_FALSE(git::find_source(fixture.request(), "a").error.empty());
    std::stop_source stop; stop.request_stop();
    ASSERT_EQ(git::find_source(fixture.request(), "a", stop.get_token()).error, "Find cancelled");
    ASSERT_FALSE(git::find_source(fixture.request(), std::string(65537, 'a')).error.empty());
    ASSERT_TRUE(git::find_source(fixture.request(), "").matches.empty());
}

TEST(column_targeted_pages_keep_the_complete_cross_boundary_match) {
    FindFixture fixture;
    fixture.write(std::string(file_page::byteLimit - 3, ' ') + "NEEDLE" + std::string(50, ' '));
    auto request = fixture.request();
    request.page = {ecs::FilePageRequest::Action::TargetLine, {}, 1, {}, 0, static_cast<int>(file_page::byteLimit - 2)};
    auto page = git::read_file(request);
    ASSERT_TRUE(page.error.empty());
    ASSERT_TRUE(page.page.begin.column > 1);
    ASSERT_TRUE(page.page.contains(1, request.page.targetColumn, 6));
    ASSERT_TRUE(page.diff.hunks[0].lines[0].find("NEEDLE") != std::string::npos);
    ASSERT_TRUE(page.raw.size() <= file_page::byteLimit);
}

int main() { RUN_ALL_TESTS(); }
