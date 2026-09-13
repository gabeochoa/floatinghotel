#include "test_framework.h"
#include "../../src/git/hunk_context.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>

struct ContextFixture {
    std::filesystem::path path;
    ContextFixture() {
        char pattern[] = "/tmp/fh-context.XXXXXX";
        path = mkdtemp(pattern);
        run({"init", "-q", "-b", "main"});
        run({"config", "user.name", "Context fixture"});
        run({"config", "user.email", "context@example.invalid"});
        run({"config", "commit.gpgsign", "false"});
    }
    ~ContextFixture() { std::filesystem::remove_all(path); }
    std::string run(std::vector<std::string> args) {
        auto result = git::git_run(path.string(), args);
        ASSERT_TRUE(result.success());
        auto out = result.stdout_str();
        if (out.ends_with('\n')) out.pop_back();
        return out;
    }
    void write(const std::string& name, const std::string& text) { std::ofstream(path / name, std::ios::binary) << text; }
    git::FileRequest request(std::string name, std::string revision = "") { return {path.string(), name, revision}; }
};

TEST(context_reads_only_unchanged_lines_from_correct_renamed_revisions) {
    ContextFixture fixture;
    std::string content;
    for (int i = 1; i <= 100; ++i) content += "line " + std::to_string(i) + " éλ\r\n";
    fixture.write("old.cpp", content);
    fixture.run({"add", "."}); fixture.run({"commit", "-qm", "base"});
    auto base = fixture.run({"rev-parse", "HEAD"});
    fixture.run({"mv", "old.cpp", "new.cpp"});
    fixture.write("new.cpp", "inserted\r\n" + content);
    fixture.run({"add", "."}); fixture.run({"commit", "-qm", "rename"});
    auto target = fixture.run({"rev-parse", "HEAD"});
    fixture.write("new.cpp", "different checkout\n");
    auto result = git::read_hunk_context(fixture.request("old.cpp", base), fixture.request("new.cpp", target), {30, 31, 20});
    ASSERT_TRUE(result.error.empty()); ASSERT_EQ(result.lines.lines.size(), 20u);
    ASSERT_EQ(result.lines.lines.front(), " line 30 éλ\r");
    ASSERT_EQ(result.lines.oldStart, 30); ASSERT_EQ(result.lines.newStart, 31);
    ASSERT_TRUE(result.bytes < 256 * 1024);
    auto staged = git::read_hunk_context(fixture.request("old.cpp", base), fixture.request("new.cpp", "INDEX"), {30, 31, 20});
    ASSERT_TRUE(staged.error.empty()); ASSERT_TRUE(staged.lines.lines == result.lines.lines);
}

TEST(context_rejects_changed_or_missing_sources_and_cancellation) {
    ContextFixture fixture;
    fixture.write("a", "one\ntwo\nthree\n");
    fixture.run({"add", "."}); fixture.run({"commit", "-qm", "base"});
    fixture.write("a", "one\nchanged\nthree\n");
    auto result = git::read_hunk_context(fixture.request("a", "INDEX"), fixture.request("a"), {1, 1, 3});
    ASSERT_FALSE(result.error.empty()); ASSERT_TRUE(result.lines.lines.empty());
    ASSERT_FALSE(git::read_hunk_context(fixture.request("a", std::string(40, 'f')), fixture.request("a"), {1, 1, 3}).error.empty());
    std::stop_source stop; stop.request_stop();
    ASSERT_FALSE(git::read_hunk_context(fixture.request("a"), fixture.request("a"), {1, 1, 3}, stop.get_token()).error.empty());
}

TEST(context_handles_eof_and_rejects_partial_long_lines) {
    ContextFixture fixture;
    fixture.write("a", "one\ntwo");
    auto result = git::read_hunk_context(fixture.request("a"), fixture.request("a"), {1, 1, 20});
    ASSERT_TRUE(result.error.empty()); ASSERT_EQ(result.lines.lines.size(), 2u);
    ASSERT_TRUE(result.lines.noNewline.contains(1));
    fixture.write("a", std::string(300000, 'x') + "\n");
    auto longLine = git::read_hunk_context(fixture.request("a"), fixture.request("a"), {1, 1, 20});
    ASSERT_FALSE(longLine.error.empty()); ASSERT_TRUE(longLine.lines.lines.empty());
}

TEST(context_ranges_merge_overlap_without_crossing_original_hunks) {
    ecs::FileDiff file;
    file.hunks = {{27, 7, 27, 7}, {57, 7, 57, 7}, {147, 7, 147, 7}};
    auto below = git::context_range(file, 0, false, 20);
    auto above = git::context_range(file, 1, true, 20, below.count);
    ASSERT_EQ(below.oldLine, 34); ASSERT_EQ(below.count, 20);
    ASSERT_EQ(above.oldLine, 54); ASSERT_EQ(above.count, 3);
    ASSERT_EQ(below.oldLine + below.count, above.oldLine);
    ASSERT_EQ(above.oldLine + above.count, file.hunks[1].oldStart);
    ASSERT_EQ(git::context_range(file, 0, true, 40).oldLine, 1);
    ASSERT_EQ(git::context_range(file, 0, false, 40).count, 23);
    ASSERT_EQ(git::context_range(file, 1, true, 40, 40).count, 0);
    ASSERT_EQ(file.hunks[0].oldCount, 7);
}

int main() { RUN_ALL_TESTS(); }
