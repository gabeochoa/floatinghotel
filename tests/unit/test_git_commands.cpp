// Unit tests for git::build_patch -- the pure function that constructs
// a unified diff patch string from FileDiff + DiffHunk structs.

#include "test_framework.h"
#include "../../src/git/git_commands.h"
#include "../../src/git/git_parser.h"

#include <filesystem>
#include <fstream>
#include <string>

// ===========================================================================
// build_patch tests
// ===========================================================================

TEST(patch_normal_modification) {
    ecs::FileDiff fd;
    fd.filePath = "src/main.cpp";

    ecs::DiffHunk hunk;
    hunk.header = "@@ -10,3 +10,4 @@ int main()";
    hunk.lines = {" int x = 1;", "-int y = 2;", "+int y = 3;", "+int z = 4;"};

    std::string patch = git::build_patch(fd, hunk);

    ASSERT_TRUE(patch.find("--- a/src/main.cpp\n") != std::string::npos);
    ASSERT_TRUE(patch.find("+++ b/src/main.cpp\n") != std::string::npos);
    ASSERT_TRUE(patch.find("@@ -10,3 +10,4 @@ int main()\n") != std::string::npos);
    ASSERT_TRUE(patch.find("-int y = 2;\n") != std::string::npos);
    ASSERT_TRUE(patch.find("+int y = 3;\n") != std::string::npos);
    ASSERT_TRUE(patch.find("+int z = 4;\n") != std::string::npos);
}

TEST(partial_hunk_preserves_unselected_lines) {
    ecs::DiffHunk hunk{1, 3, 1, 4, "@@ -1,3 +1,4 @@",
                        {" keep", "-old", "+replacement", "+extra", " tail"}};
    auto partial = git::selected_lines_hunk(hunk, {3});
    ASSERT_TRUE(partial.has_value());
    ASSERT_EQ(partial->lines, (std::vector<std::string>{" keep", " old", "+extra", " tail"}));
    ASSERT_EQ(partial->oldCount, 3);
    ASSERT_EQ(partial->newCount, 4);
    ASSERT_FALSE(git::selected_lines_hunk(hunk, {0}).has_value());
    auto deletion = git::selected_lines_hunk(hunk, {1});
    ASSERT_EQ(deletion->newCount, 2);
    ASSERT_EQ(deletion->lines, (std::vector<std::string>{" keep", "-old", " tail"}));
    ecs::DiffHunk removed{1, 2, 0, 0, "@@ -1,2 +0,0 @@", {"-first", "-second"}};
    auto partialRemoval = git::selected_lines_hunk(removed, {0});
    ASSERT_EQ(partialRemoval->newStart, 1);
    ASSERT_EQ(partialRemoval->newCount, 1);
}

TEST(patch_new_file) {
    ecs::FileDiff fd;
    fd.filePath = "new_file.txt";
    fd.isNew = true;

    ecs::DiffHunk hunk;
    hunk.header = "@@ -0,0 +1,2 @@";
    hunk.lines = {"+line1", "+line2"};

    std::string patch = git::build_patch(fd, hunk);

    ASSERT_TRUE(patch.find("--- /dev/null\n") != std::string::npos);
    ASSERT_TRUE(patch.find("+++ b/new_file.txt\n") != std::string::npos);
    // Should NOT have --- a/
    ASSERT_TRUE(patch.find("--- a/") == std::string::npos);
}

TEST(patch_deleted_file) {
    ecs::FileDiff fd;
    fd.filePath = "old_file.txt";
    fd.isDeleted = true;

    ecs::DiffHunk hunk;
    hunk.header = "@@ -1,2 +0,0 @@";
    hunk.lines = {"-line1", "-line2"};

    std::string patch = git::build_patch(fd, hunk);

    ASSERT_TRUE(patch.find("--- a/old_file.txt\n") != std::string::npos);
    ASSERT_TRUE(patch.find("+++ /dev/null\n") != std::string::npos);
    // Should NOT have +++ b/
    ASSERT_TRUE(patch.find("+++ b/") == std::string::npos);
}

TEST(patch_with_old_path) {
    ecs::FileDiff fd;
    fd.filePath = "new_name.cpp";
    fd.oldPath = "old_name.cpp";

    ecs::DiffHunk hunk;
    hunk.header = "@@ -1,1 +1,1 @@";
    hunk.lines = {"-old", "+new"};

    std::string patch = git::build_patch(fd, hunk);

    // --- should use oldPath
    ASSERT_TRUE(patch.find("--- a/old_name.cpp\n") != std::string::npos);
    // +++ should use filePath
    ASSERT_TRUE(patch.find("+++ b/new_name.cpp\n") != std::string::npos);
}

TEST(patch_empty_old_path_uses_file_path) {
    ecs::FileDiff fd;
    fd.filePath = "same.cpp";
    // oldPath left empty (default)

    ecs::DiffHunk hunk;
    hunk.header = "@@ -1,1 +1,1 @@";
    hunk.lines = {"-a", "+b"};

    std::string patch = git::build_patch(fd, hunk);

    ASSERT_TRUE(patch.find("--- a/same.cpp\n") != std::string::npos);
    ASSERT_TRUE(patch.find("+++ b/same.cpp\n") != std::string::npos);
}

TEST(patch_empty_hunk_lines) {
    ecs::FileDiff fd;
    fd.filePath = "empty.txt";

    ecs::DiffHunk hunk;
    hunk.header = "@@ -0,0 +0,0 @@";
    // No lines

    std::string patch = git::build_patch(fd, hunk);

    ASSERT_TRUE(patch.find("--- a/empty.txt\n") != std::string::npos);
    ASSERT_TRUE(patch.find("+++ b/empty.txt\n") != std::string::npos);
    ASSERT_TRUE(patch.find("@@ -0,0 +0,0 @@\n") != std::string::npos);
}

TEST(patch_single_addition) {
    ecs::FileDiff fd;
    fd.filePath = "add.txt";

    ecs::DiffHunk hunk;
    hunk.header = "@@ -1,0 +1,1 @@";
    hunk.lines = {"+new line"};

    std::string patch = git::build_patch(fd, hunk);

    ASSERT_TRUE(patch.find("+new line\n") != std::string::npos);
}

TEST(patch_single_deletion) {
    ecs::FileDiff fd;
    fd.filePath = "del.txt";

    ecs::DiffHunk hunk;
    hunk.header = "@@ -1,1 +1,0 @@";
    hunk.lines = {"-removed line"};

    std::string patch = git::build_patch(fd, hunk);

    ASSERT_TRUE(patch.find("-removed line\n") != std::string::npos);
}

TEST(patch_context_lines) {
    ecs::FileDiff fd;
    fd.filePath = "ctx.txt";

    ecs::DiffHunk hunk;
    hunk.header = "@@ -1,4 +1,4 @@";
    hunk.lines = {" before", "-old", "+new", " after"};

    std::string patch = git::build_patch(fd, hunk);

    ASSERT_TRUE(patch.find(" before\n") != std::string::npos);
    ASSERT_TRUE(patch.find("-old\n") != std::string::npos);
    ASSERT_TRUE(patch.find("+new\n") != std::string::npos);
    ASSERT_TRUE(patch.find(" after\n") != std::string::npos);
}

TEST(patch_structure_order) {
    ecs::FileDiff fd;
    fd.filePath = "order.txt";

    ecs::DiffHunk hunk;
    hunk.header = "@@ -1,1 +1,1 @@";
    hunk.lines = {"-a", "+b"};

    std::string patch = git::build_patch(fd, hunk);

    // Verify order: --- then +++ then @@ then lines
    auto pos_minus = patch.find("--- a/");
    auto pos_plus = patch.find("+++ b/");
    auto pos_hunk = patch.find("@@ -1,1 +1,1 @@");
    auto pos_line = patch.find("-a\n");

    ASSERT_TRUE(pos_minus != std::string::npos);
    ASSERT_TRUE(pos_plus != std::string::npos);
    ASSERT_TRUE(pos_hunk != std::string::npos);
    ASSERT_TRUE(pos_line != std::string::npos);
    ASSERT_TRUE(pos_minus < pos_plus);
    ASSERT_TRUE(pos_plus < pos_hunk);
    ASSERT_TRUE(pos_hunk < pos_line);
}

TEST(patch_new_and_deleted_both_set) {
    // Edge case: both isNew and isDeleted (shouldn't happen in practice)
    ecs::FileDiff fd;
    fd.filePath = "weird.txt";
    fd.isNew = true;
    fd.isDeleted = true;

    ecs::DiffHunk hunk;
    hunk.header = "@@ -0,0 +0,0 @@";

    std::string patch = git::build_patch(fd, hunk);

    ASSERT_TRUE(patch.find("--- /dev/null\n") != std::string::npos);
    ASSERT_TRUE(patch.find("+++ /dev/null\n") != std::string::npos);
}

TEST(patch_all_lines_get_newline) {
    ecs::FileDiff fd;
    fd.filePath = "multi.txt";

    ecs::DiffHunk hunk;
    hunk.header = "@@ -1,3 +1,4 @@";
    hunk.lines = {" line1", " line2", "-line3", "+line3_new", "+line4"};

    std::string patch = git::build_patch(fd, hunk);

    ASSERT_TRUE(patch.find(" line1\n") != std::string::npos);
    ASSERT_TRUE(patch.find(" line2\n") != std::string::npos);
    ASSERT_TRUE(patch.find("-line3\n") != std::string::npos);
    ASSERT_TRUE(patch.find("+line3_new\n") != std::string::npos);
    ASSERT_TRUE(patch.find("+line4\n") != std::string::npos);
}

// ===========================================================================

// ===========================================================================
// change_chunks: ~20-line review chunks cut at context seams
// ===========================================================================

TEST(chunks_cut_at_context_and_keep_long_replacements_whole) {
    ecs::DiffHunk hunk;
    for (int run = 0; run < 3; ++run) {
        for (int i = 0; i < 8; ++i) hunk.lines.push_back("-old");
        for (int i = 0; i < 4; ++i) hunk.lines.push_back("+new");
        hunk.lines.push_back(" context");
    }
    for (int i = 0; i < 30; ++i) hunk.lines.push_back("+long");
    auto chunks = git::change_chunks(hunk, 20);
    // 12 + 12 > 20, so each 12-line replacement is a chunk; the 30-line run stays whole.
    ASSERT_EQ(chunks.size(), 4u);
    ASSERT_EQ(*chunks[0].begin(), 0u); ASSERT_EQ(*chunks[0].rbegin(), 11u);
    ASSERT_EQ(*chunks[1].begin(), 13u);
    ASSERT_EQ(chunks[3].size(), 30u);
    ASSERT_EQ(git::change_chunks(hunk, 100).size(), 1u);
    ASSERT_TRUE(git::change_chunks(ecs::DiffHunk{}).empty());
}

namespace {
struct ChunkRepo {
    std::filesystem::path path;
    ChunkRepo() {
        char pattern[] = "/tmp/fh-chunks.XXXXXX";
        path = mkdtemp(pattern);
        for (auto args : std::vector<std::vector<std::string>>{{"init", "-q"}, {"config", "user.email", "t@e.invalid"},
                {"config", "user.name", "t"}, {"config", "commit.gpgsign", "false"}})
            git::git_run(path.string(), args);
    }
    ~ChunkRepo() { std::filesystem::remove_all(path); }
    void write(const std::string& text) { std::ofstream(path / "a.txt", std::ios::binary) << text; }
    std::string run(std::vector<std::string> args) { return git::git_run(path.string(), args).stdout_str(); }
    ecs::FileDiff diff() { return git::parse_diff(run({"diff", "-U3", "--", "a.txt"})).at(0); }
    std::vector<std::set<size_t>> only(const ecs::FileDiff& file, size_t hunk, const std::set<size_t>& lines) {
        std::vector<std::set<size_t>> selected(file.hunks.size());
        selected[hunk] = lines;
        return selected;
    }
};
std::string numbered(int count, int changedFrom = 0, int changedTo = -1, bool finalNewline = true) {
    std::string text;
    for (int i = 1; i <= count; ++i)
        text += (i >= changedFrom && i <= changedTo ? "changed " : "line ") + std::to_string(i) + (i < count || finalNewline ? "\n" : "");
    return text;
}
}

TEST(chunks_stage_out_of_order_adjacent_and_without_final_newline) {
    ChunkRepo repo;
    repo.write(numbered(60));
    repo.run({"add", "a.txt"});
    repo.run({"commit", "-qm", "base"});
    // Two adjacent 12-line replacements split by one context line, then a
    // change to the last line that also drops the final newline.
    std::string edited;
    for (int i = 1; i <= 60; ++i) {
        const bool changed = (i >= 10 && i <= 15) || (i >= 17 && i <= 22) || i == 60;
        edited += (changed ? "changed " : "line ") + std::to_string(i) + (i < 60 ? "\n" : "");
    }
    repo.write(edited);
    auto file = repo.diff();
    ASSERT_EQ(file.hunks.size(), 2u);
    auto chunks = git::change_chunks(file.hunks[0], 20);
    ASSERT_EQ(chunks.size(), 2u);
    // Stage the later chunk first; the earlier one still applies afterwards.
    ASSERT_EQ(git::stage_selected_lines(repo.path.string(), file, repo.only(file, 0, chunks[1])).exit_code(), 0);
    auto cached = repo.run({"diff", "--cached"});
    ASSERT_TRUE(cached.find("+changed 17") != std::string::npos && cached.find("+changed 10") == std::string::npos);
    file = repo.diff();
    chunks = git::change_chunks(file.hunks[0], 20);
    ASSERT_EQ(chunks.size(), 1u);
    ASSERT_EQ(git::stage_selected_lines(repo.path.string(), file, repo.only(file, 0, chunks[0])).exit_code(), 0);
    file = repo.diff();
    ASSERT_EQ(file.hunks.size(), 1u);
    ASSERT_EQ(git::stage_selected_lines(repo.path.string(), file, repo.only(file, 0, git::change_chunks(file.hunks[0])[0])).exit_code(), 0);
    ASSERT_EQ(repo.run({"diff"}), "");
    ASSERT_EQ(repo.run({"show", ":a.txt"}), edited);
}

TEST(chunk_patch_is_rejected_when_the_index_moved_underneath) {
    ChunkRepo repo;
    repo.write(numbered(30));
    repo.run({"add", "a.txt"});
    repo.run({"commit", "-qm", "base"});
    repo.write(numbered(30, 5, 8));
    auto file = repo.diff();
    // Someone else stages a conflicting edit before ours lands.
    repo.write(numbered(30, 4, 9));
    repo.run({"add", "a.txt"});
    auto result = git::stage_selected_lines(repo.path.string(), file, repo.only(file, 0, git::change_chunks(file.hunks[0])[0]));
    ASSERT_TRUE(result.exit_code() != 0);
    ASSERT_EQ(repo.run({"show", ":a.txt"}), numbered(30, 4, 9));
}

int main() {
    printf("=== git_commands tests ===\n");
    RUN_ALL_TESTS();
}
