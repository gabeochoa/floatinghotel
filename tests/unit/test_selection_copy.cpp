#include "test_framework.h"
#include "../../src/git/selection_copy.h"
#include "../../src/git/git_runner.h"
#include "../../src/util/file_page.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>

struct CopyFixture {
    std::filesystem::path path;
    CopyFixture() { char pattern[] = "/tmp/fh-copy.XXXXXX"; path = mkdtemp(pattern); }
    ~CopyFixture() { std::filesystem::remove_all(path); }
    void write(const std::string& text) { std::ofstream(path / "a.txt", std::ios::binary) << text; }
    git::FileRequest request(std::string revision = "") const { return {path.string(), "a.txt", std::move(revision)}; }
    reading::CodeSelection range(int firstLine, int firstColumn, int lastLine, int lastColumn) const {
        return {{"a.txt", reading::DiffSide::After, firstLine, firstColumn},
                {"a.txt", reading::DiffSide::After, lastLine, lastColumn}, {"a.txt", reading::WorkingTree{}}};
    }
};

TEST(copy_scans_past_a_page_and_preserves_original_whitespace) {
    CopyFixture fixture;
    std::string text;
    for (int i = 0; i < 5000; ++i) text += "\téλ  \r\n";
    fixture.write(text + "tail");
    auto result = git::copy_source_selection(fixture.request(), fixture.range(1, 1, 5001, 1), false);
    ASSERT_TRUE(result.error.empty()); ASSERT_EQ(result.text, text);
    ASSERT_TRUE(result.maxPageBytes <= file_page::byteLimit);
    auto reversed = git::copy_source_selection(fixture.request(), fixture.range(5001, 1, 1, 1), true);
    ASSERT_EQ(reversed.text, "a.txt:L1-5001\n" + text);
}

TEST(copy_joins_long_unicode_line_fragments_without_extra_newlines) {
    CopyFixture fixture;
    std::string text;
    for (int i = 0; i < 300000; ++i) text += "é";
    fixture.write(text + " EOF");
    auto result = git::copy_source_selection(fixture.request(), fixture.range(1, 2, 1, 300005), false);
    ASSERT_TRUE(result.error.empty()); ASSERT_EQ(result.text, text.substr(2) + " EOF");
    ASSERT_TRUE(result.maxPageBytes <= file_page::byteLimit);
}

TEST(copy_refuses_over_limit_without_returning_a_partial_copy) {
    CopyFixture fixture;
    fixture.write(std::string(git::selection_copy_limit + 1, 'x'));
    auto result = git::copy_source_selection(fixture.request(), fixture.range(1, 1, 1, git::selection_copy_limit + 2), false);
    ASSERT_TRUE(result.text.empty()); ASSERT_EQ(result.error, "Selection exceeds the 8 MiB copy limit");
    auto exact = git::copy_source_selection(fixture.request(), fixture.range(1, 1, 1, git::selection_copy_limit + 1), false);
    ASSERT_TRUE(exact.error.empty()); ASSERT_EQ(exact.text.size(), git::selection_copy_limit);
}

TEST(copy_rejects_changed_source_identity_and_missing_positions) {
    CopyFixture fixture;
    fixture.write("first\nsecond");
    auto content = git::read_file(fixture.request());
    auto selection = fixture.range(1, 1, 2, 7);
    selection.sourceIdentity = content.page.sourceIdentity;
    fixture.write("replacement\ntext");
    auto changed = git::copy_source_selection(fixture.request(), selection, false);
    ASSERT_TRUE(changed.text.empty()); ASSERT_FALSE(changed.error.empty());
    auto missing = git::copy_source_selection(fixture.request(), fixture.range(1, 1, 2, 99), false);
    ASSERT_TRUE(missing.text.empty()); ASSERT_FALSE(missing.error.empty());
}

TEST(copy_cancellation_returns_no_partial_text) {
    CopyFixture fixture;
    fixture.write("text");
    std::stop_source cancel; cancel.request_stop();
    auto result = git::copy_source_selection(fixture.request(), fixture.range(1, 1, 1, 5), false, cancel.get_token());
    ASSERT_TRUE(result.text.empty()); ASSERT_EQ(result.error, "Copy cancelled");
}

TEST(copy_decodes_utf16_and_preserves_line_endings) {
    CopyFixture fixture;
    std::string bytes = "\xff\xfe";
    for (char c : std::string("one\r\ntwo")) { bytes += c; bytes += '\0'; }
    fixture.write(bytes);
    auto result = git::copy_source_selection(fixture.request(), fixture.range(1, 1, 2, 4), false);
    ASSERT_TRUE(result.error.empty()); ASSERT_EQ(result.text, "one\r\ntwo");
}

TEST(copy_retains_historical_and_index_source_contents) {
    CopyFixture fixture;
    auto run = [&](std::vector<std::string> args) { return git::git_run(fixture.path.string(), args); };
    ASSERT_TRUE(run({"init", "-q"}).success());
    ASSERT_TRUE(run({"config", "user.name", "Copy test"}).success());
    ASSERT_TRUE(run({"config", "user.email", "copy@example.invalid"}).success());
    ASSERT_TRUE(run({"config", "commit.gpgsign", "false"}).success());
    fixture.write("original"); ASSERT_TRUE(run({"add", "."}).success());
    ASSERT_TRUE(run({"commit", "-qm", "base"}).success());
    fixture.write("staged"); ASSERT_TRUE(run({"add", "."}).success()); fixture.write("working");
    auto historical = git::copy_source_selection(fixture.request("HEAD"), fixture.range(1, 1, 1, 9), false);
    auto index = git::copy_source_selection(fixture.request("INDEX"), fixture.range(1, 1, 1, 7), false);
    ASSERT_TRUE(historical.error.empty()); ASSERT_EQ(historical.text, "original");
    ASSERT_TRUE(index.error.empty()); ASSERT_EQ(index.text, "staged");
}

int main() { RUN_ALL_TESTS(); }
