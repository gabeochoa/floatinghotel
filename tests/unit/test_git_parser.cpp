// Unit tests for git::parse_status, git::parse_log, git::parse_diff,
// git::parse_branch_list.
//
// Each test feeds realistic git-format strings to the parser and verifies
// the returned structs.

#include "test_framework.h"
#include "../../src/git/git_parser.h"

#include <string>

// ===========================================================================
// parse_status tests
// ===========================================================================

TEST(status_empty_output) {
    auto r = git::parse_status("");
    ASSERT_TRUE(r.branchName.empty());
    ASSERT_TRUE(r.stagedFiles.empty());
    ASSERT_TRUE(r.unstagedFiles.empty());
    ASSERT_TRUE(r.untrackedFiles.empty());
}

TEST(status_branch_name) {
    std::string input = "# branch.head main\n";
    auto r = git::parse_status(input);
    ASSERT_STREQ(r.branchName, "main");
    ASSERT_FALSE(r.isDetachedHead);
}

TEST(status_detached_head) {
    std::string input = "# branch.head (detached)\n";
    auto r = git::parse_status(input);
    ASSERT_STREQ(r.branchName, "(detached)");
    ASSERT_TRUE(r.isDetachedHead);
}

TEST(status_upstream_branch) {
    std::string input =
        "# branch.head main\n"
        "# branch.upstream origin/main\n";
    auto r = git::parse_status(input);
    ASSERT_STREQ(r.upstreamBranch, "origin/main");
}

TEST(status_ahead_behind) {
    std::string input =
        "# branch.head main\n"
        "# branch.upstream origin/main\n"
        "# branch.ab +3 -1\n";
    auto r = git::parse_status(input);
    ASSERT_EQ(r.aheadCount, 3);
    ASSERT_EQ(r.behindCount, 1);
}

TEST(status_ahead_only) {
    std::string input =
        "# branch.head feature\n"
        "# branch.ab +5 -0\n";
    auto r = git::parse_status(input);
    ASSERT_EQ(r.aheadCount, 5);
    ASSERT_EQ(r.behindCount, 0);
}

TEST(status_ordinary_staged_file) {
    // Porcelain v2 type 1: ordinary changed entry
    // 1 XY sub mH mI mW hH hI path
    // M. = staged modification, no worktree change
    std::string input =
        "1 M. N... 100644 100644 100644 "
        "abc1234abc1234abc1234abc1234abc1234abc1234 "
        "def5678def5678def5678def5678def5678def5678 src/main.cpp\n";
    auto r = git::parse_status(input);
    ASSERT_EQ(r.stagedFiles.size(), static_cast<size_t>(1));
    ASSERT_TRUE(r.unstagedFiles.empty());
    ASSERT_STREQ(r.stagedFiles[0].path, "src/main.cpp");
    ASSERT_EQ(r.stagedFiles[0].indexStatus, 'M');
    ASSERT_EQ(r.stagedFiles[0].workTreeStatus, '.');
}

TEST(status_ordinary_unstaged_file) {
    // .M = no staged change, worktree modification
    std::string input =
        "1 .M N... 100644 100644 100644 "
        "abc1234abc1234abc1234abc1234abc1234abc1234 "
        "def5678def5678def5678def5678def5678def5678 README.md\n";
    auto r = git::parse_status(input);
    ASSERT_TRUE(r.stagedFiles.empty());
    ASSERT_EQ(r.unstagedFiles.size(), static_cast<size_t>(1));
    ASSERT_STREQ(r.unstagedFiles[0].path, "README.md");
}

TEST(status_both_staged_and_unstaged) {
    // MM = staged modification AND worktree modification
    std::string input =
        "1 MM N... 100644 100644 100644 "
        "abc1234abc1234abc1234abc1234abc1234abc1234 "
        "def5678def5678def5678def5678def5678def5678 file.txt\n";
    auto r = git::parse_status(input);
    ASSERT_EQ(r.stagedFiles.size(), static_cast<size_t>(1));
    ASSERT_EQ(r.unstagedFiles.size(), static_cast<size_t>(1));
    ASSERT_STREQ(r.stagedFiles[0].path, "file.txt");
    ASSERT_STREQ(r.unstagedFiles[0].path, "file.txt");
}

TEST(status_added_file) {
    // A. = staged add, no worktree change
    std::string input =
        "1 A. N... 000000 100644 100644 "
        "0000000000000000000000000000000000000000 "
        "abc1234abc1234abc1234abc1234abc1234abc1234 new_file.rs\n";
    auto r = git::parse_status(input);
    ASSERT_EQ(r.stagedFiles.size(), static_cast<size_t>(1));
    ASSERT_EQ(r.stagedFiles[0].indexStatus, 'A');
}

TEST(status_deleted_file) {
    // D. = staged delete
    std::string input =
        "1 D. N... 100644 000000 000000 "
        "abc1234abc1234abc1234abc1234abc1234abc1234 "
        "0000000000000000000000000000000000000000 old.txt\n";
    auto r = git::parse_status(input);
    ASSERT_EQ(r.stagedFiles.size(), static_cast<size_t>(1));
    ASSERT_EQ(r.stagedFiles[0].indexStatus, 'D');
    ASSERT_STREQ(r.stagedFiles[0].path, "old.txt");
}

TEST(status_rename_entry) {
    // Type 2 = rename/copy
    // 2 R. N... 100644 100644 100644 hash hash R100 new_name.cpp\told_name.cpp
    std::string input =
        "2 R. N... 100644 100644 100644 "
        "abc1234abc1234abc1234abc1234abc1234abc1234 "
        "def5678def5678def5678def5678def5678def5678 "
        "R100 new_name.cpp\told_name.cpp\n";
    auto r = git::parse_status(input);
    ASSERT_EQ(r.stagedFiles.size(), static_cast<size_t>(1));
    ASSERT_STREQ(r.stagedFiles[0].path, "new_name.cpp");
    ASSERT_STREQ(r.stagedFiles[0].origPath, "old_name.cpp");
    ASSERT_EQ(r.stagedFiles[0].indexStatus, 'R');
}

TEST(status_untracked_file) {
    std::string input = "? untracked_file.txt\n";
    auto r = git::parse_status(input);
    ASSERT_EQ(r.untrackedFiles.size(), static_cast<size_t>(1));
    ASSERT_STREQ(r.untrackedFiles[0], "untracked_file.txt");
}

TEST(status_ignored_file_skipped) {
    std::string input = "! ignored_file.o\n";
    auto r = git::parse_status(input);
    ASSERT_TRUE(r.untrackedFiles.empty());
    ASSERT_TRUE(r.stagedFiles.empty());
    ASSERT_TRUE(r.unstagedFiles.empty());
}

TEST(status_multiple_entries) {
    std::string input =
        "# branch.head develop\n"
        "# branch.upstream origin/develop\n"
        "# branch.ab +2 -0\n"
        "1 M. N... 100644 100644 100644 "
        "abc1234abc1234abc1234abc1234abc1234abc1234 "
        "def5678def5678def5678def5678def5678def5678 src/a.cpp\n"
        "1 .M N... 100644 100644 100644 "
        "abc1234abc1234abc1234abc1234abc1234abc1234 "
        "def5678def5678def5678def5678def5678def5678 src/b.cpp\n"
        "? new_file.txt\n"
        "? another.txt\n";
    auto r = git::parse_status(input);
    ASSERT_STREQ(r.branchName, "develop");
    ASSERT_STREQ(r.upstreamBranch, "origin/develop");
    ASSERT_EQ(r.aheadCount, 2);
    ASSERT_EQ(r.stagedFiles.size(), static_cast<size_t>(1));
    ASSERT_EQ(r.unstagedFiles.size(), static_cast<size_t>(1));
    ASSERT_EQ(r.untrackedFiles.size(), static_cast<size_t>(2));
}

TEST(status_unmerged_entry) {
    // u XY sub m1 m2 m3 mW h1 h2 h3 path
    std::string input =
        "u UU N... 100644 100644 100644 100644 "
        "abc1234abc1234abc1234abc1234abc1234abc1234 "
        "def5678def5678def5678def5678def5678def5678 "
        "111222333444555666777888999000aaabbbcccddd conflict.txt\n";
    auto r = git::parse_status(input);
    // Unmerged entries appear in both lists
    ASSERT_EQ(r.stagedFiles.size(), static_cast<size_t>(1));
    ASSERT_EQ(r.unstagedFiles.size(), static_cast<size_t>(1));
    ASSERT_STREQ(r.stagedFiles[0].path, "conflict.txt");
    ASSERT_EQ(r.stagedFiles[0].indexStatus, 'U');
    ASSERT_EQ(r.stagedFiles[0].workTreeStatus, 'U');
}

TEST(status_blank_lines_ignored) {
    std::string input =
        "\n"
        "# branch.head main\n"
        "\n"
        "? foo.txt\n"
        "\n";
    auto r = git::parse_status(input);
    ASSERT_STREQ(r.branchName, "main");
    ASSERT_EQ(r.untrackedFiles.size(), static_cast<size_t>(1));
}

// ===========================================================================
// parse_log tests
// ===========================================================================

// Helper to build a log line with NUL separators
static std::string make_log_line(const std::string& hash,
                                  const std::string& shortHash,
                                  const std::string& subject,
                                  const std::string& author,
                                  const std::string& date,
                                  const std::string& decorations = "") {
    std::string line;
    line += hash;
    line += '\0';
    line += shortHash;
    line += '\0';
    line += subject;
    line += '\0';
    line += author;
    line += '\0';
    line += date;
    line += '\0';
    line += decorations;
    return line;
}

TEST(log_empty_output) {
    auto entries = git::parse_log("");
    ASSERT_TRUE(entries.empty());
}

TEST(log_single_commit) {
    std::string input = make_log_line(
        "abc123def456abc123def456abc123def456abc123",
        "abc123d",
        "Initial commit",
        "Alice",
        "2025-01-15T10:30:00-05:00",
        "HEAD -> main, origin/main");
    input += "\n";

    auto entries = git::parse_log(input);
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    ASSERT_STREQ(entries[0].hash, "abc123def456abc123def456abc123def456abc123");
    ASSERT_STREQ(entries[0].shortHash, "abc123d");
    ASSERT_STREQ(entries[0].subject, "Initial commit");
    ASSERT_STREQ(entries[0].author, "Alice");
    ASSERT_STREQ(entries[0].authorDate, "2025-01-15T10:30:00-05:00");
    ASSERT_STREQ(entries[0].decorations, "HEAD -> main, origin/main");
}

TEST(log_multiple_commits) {
    std::string input;
    input += make_log_line("aaaa", "aaa", "Fix bug", "Bob", "2025-01-15");
    input += "\n";
    input += make_log_line("bbbb", "bbb", "Add feature", "Alice", "2025-01-14");
    input += "\n";
    input += make_log_line("cccc", "ccc", "Initial", "Bob", "2025-01-13", "tag: v1.0");
    input += "\n";

    auto entries = git::parse_log(input);
    ASSERT_EQ(entries.size(), static_cast<size_t>(3));
    ASSERT_STREQ(entries[0].subject, "Fix bug");
    ASSERT_STREQ(entries[1].subject, "Add feature");
    ASSERT_STREQ(entries[2].subject, "Initial");
    ASSERT_STREQ(entries[2].decorations, "tag: v1.0");
}

TEST(log_no_decorations) {
    std::string input = make_log_line("hash1", "h1", "Commit", "Dev", "2025-01-01");
    input += "\n";

    auto entries = git::parse_log(input);
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    ASSERT_TRUE(entries[0].decorations.empty());
}

TEST(log_blank_lines_ignored) {
    std::string input;
    input += "\n";
    input += make_log_line("aaaa", "aaa", "Commit 1", "Dev", "2025-01-01");
    input += "\n\n";
    input += make_log_line("bbbb", "bbb", "Commit 2", "Dev", "2025-01-02");
    input += "\n";

    auto entries = git::parse_log(input);
    ASSERT_EQ(entries.size(), static_cast<size_t>(2));
}

TEST(log_too_few_fields_skipped) {
    // Only 3 NUL-separated fields -- fewer than the required 5
    std::string input = "abc";
    input += '\0';
    input += "short";
    input += '\0';
    input += "subject";
    input += "\n";

    auto entries = git::parse_log(input);
    ASSERT_TRUE(entries.empty());
}

// ===========================================================================
// parse_diff tests
// ===========================================================================

TEST(diff_empty_output) {
    auto diffs = git::parse_diff("");
    ASSERT_TRUE(diffs.empty());
}

TEST(diff_simple_modification) {
    std::string input =
        "diff --git a/src/main.cpp b/src/main.cpp\n"
        "--- a/src/main.cpp\n"
        "+++ b/src/main.cpp\n"
        "@@ -10,3 +10,4 @@ int main() {\n"
        "     int x = 1;\n"
        "-    int y = 2;\n"
        "+    int y = 3;\n"
        "+    int z = 4;\n";

    auto diffs = git::parse_diff(input);
    ASSERT_EQ(diffs.size(), static_cast<size_t>(1));
    ASSERT_STREQ(diffs[0].filePath, "src/main.cpp");
    ASSERT_FALSE(diffs[0].isNew);
    ASSERT_FALSE(diffs[0].isDeleted);
    ASSERT_FALSE(diffs[0].isRenamed);
    ASSERT_EQ(diffs[0].additions, 2);
    ASSERT_EQ(diffs[0].deletions, 1);
    ASSERT_EQ(diffs[0].hunks.size(), static_cast<size_t>(1));
    ASSERT_EQ(diffs[0].hunks[0].oldStart, 10);
    ASSERT_EQ(diffs[0].hunks[0].oldCount, 3);
    ASSERT_EQ(diffs[0].hunks[0].newStart, 10);
    ASSERT_EQ(diffs[0].hunks[0].newCount, 4);
    ASSERT_EQ(diffs[0].hunks[0].lines.size(), static_cast<size_t>(4));
}

TEST(diff_new_file) {
    std::string input =
        "diff --git a/hello.txt b/hello.txt\n"
        "--- /dev/null\n"
        "+++ b/hello.txt\n"
        "@@ -0,0 +1,2 @@\n"
        "+Hello\n"
        "+World\n";

    auto diffs = git::parse_diff(input);
    ASSERT_EQ(diffs.size(), static_cast<size_t>(1));
    ASSERT_TRUE(diffs[0].isNew);
    ASSERT_FALSE(diffs[0].isDeleted);
    ASSERT_STREQ(diffs[0].filePath, "hello.txt");
    ASSERT_EQ(diffs[0].additions, 2);
    ASSERT_EQ(diffs[0].deletions, 0);
}

TEST(diff_deleted_file) {
    std::string input =
        "diff --git a/old.txt b/old.txt\n"
        "--- a/old.txt\n"
        "+++ /dev/null\n"
        "@@ -1,3 +0,0 @@\n"
        "-line 1\n"
        "-line 2\n"
        "-line 3\n";

    auto diffs = git::parse_diff(input);
    ASSERT_EQ(diffs.size(), static_cast<size_t>(1));
    ASSERT_FALSE(diffs[0].isNew);
    ASSERT_TRUE(diffs[0].isDeleted);
    ASSERT_EQ(diffs[0].additions, 0);
    ASSERT_EQ(diffs[0].deletions, 3);
}

TEST(diff_renamed_file) {
    std::string input =
        "diff --git a/old_name.cpp b/new_name.cpp\n"
        "rename from old_name.cpp\n"
        "rename to new_name.cpp\n";

    auto diffs = git::parse_diff(input);
    ASSERT_EQ(diffs.size(), static_cast<size_t>(1));
    ASSERT_TRUE(diffs[0].isRenamed);
    ASSERT_STREQ(diffs[0].oldPath, "old_name.cpp");
    ASSERT_STREQ(diffs[0].filePath, "new_name.cpp");
}

TEST(diff_binary_file) {
    std::string input =
        "diff --git a/image.png b/image.png\n"
        "Binary files a/image.png and b/image.png differ\n";

    auto diffs = git::parse_diff(input);
    ASSERT_EQ(diffs.size(), static_cast<size_t>(1));
    ASSERT_TRUE(diffs[0].isBinary);
}

TEST(diff_multiple_files) {
    std::string input =
        "diff --git a/file1.cpp b/file1.cpp\n"
        "--- a/file1.cpp\n"
        "+++ b/file1.cpp\n"
        "@@ -1,2 +1,3 @@\n"
        " existing\n"
        "+added\n"
        " more\n"
        "diff --git a/file2.h b/file2.h\n"
        "--- a/file2.h\n"
        "+++ b/file2.h\n"
        "@@ -5,1 +5,1 @@\n"
        "-old line\n"
        "+new line\n";

    auto diffs = git::parse_diff(input);
    ASSERT_EQ(diffs.size(), static_cast<size_t>(2));
    ASSERT_STREQ(diffs[0].filePath, "file1.cpp");
    ASSERT_EQ(diffs[0].additions, 1);
    ASSERT_EQ(diffs[0].deletions, 0);
    ASSERT_STREQ(diffs[1].filePath, "file2.h");
    ASSERT_EQ(diffs[1].additions, 1);
    ASSERT_EQ(diffs[1].deletions, 1);
}

TEST(diff_multiple_hunks) {
    std::string input =
        "diff --git a/big.cpp b/big.cpp\n"
        "--- a/big.cpp\n"
        "+++ b/big.cpp\n"
        "@@ -1,3 +1,4 @@\n"
        " line1\n"
        "+inserted\n"
        " line2\n"
        " line3\n"
        "@@ -20,2 +21,3 @@\n"
        " line20\n"
        "+also inserted\n"
        "+and another\n"
        " line21\n";

    auto diffs = git::parse_diff(input);
    ASSERT_EQ(diffs.size(), static_cast<size_t>(1));
    ASSERT_EQ(diffs[0].hunks.size(), static_cast<size_t>(2));
    ASSERT_EQ(diffs[0].hunks[0].oldStart, 1);
    ASSERT_EQ(diffs[0].hunks[0].newStart, 1);
    ASSERT_EQ(diffs[0].hunks[1].oldStart, 20);
    ASSERT_EQ(diffs[0].hunks[1].newStart, 21);
    ASSERT_EQ(diffs[0].additions, 3);
}

TEST(diff_single_line_hunk_no_comma) {
    // @@ -1 +1 @@ -- no comma means count defaults to 1
    std::string input =
        "diff --git a/one.txt b/one.txt\n"
        "--- a/one.txt\n"
        "+++ b/one.txt\n"
        "@@ -1 +1 @@\n"
        "-old\n"
        "+new\n";

    auto diffs = git::parse_diff(input);
    ASSERT_EQ(diffs.size(), static_cast<size_t>(1));
    ASSERT_EQ(diffs[0].hunks.size(), static_cast<size_t>(1));
    ASSERT_EQ(diffs[0].hunks[0].oldStart, 1);
    ASSERT_EQ(diffs[0].hunks[0].oldCount, 1);
    ASSERT_EQ(diffs[0].hunks[0].newStart, 1);
    ASSERT_EQ(diffs[0].hunks[0].newCount, 1);
}

TEST(diff_context_lines_preserved) {
    std::string input =
        "diff --git a/ctx.txt b/ctx.txt\n"
        "--- a/ctx.txt\n"
        "+++ b/ctx.txt\n"
        "@@ -1,5 +1,5 @@\n"
        " context before\n"
        " more context\n"
        "-removed\n"
        "+added\n"
        " context after\n"
        " final context\n";

    auto diffs = git::parse_diff(input);
    ASSERT_EQ(diffs[0].hunks[0].lines.size(), static_cast<size_t>(6));
    ASSERT_STREQ(diffs[0].hunks[0].lines[0], " context before");
    ASSERT_STREQ(diffs[0].hunks[0].lines[2], "-removed");
    ASSERT_STREQ(diffs[0].hunks[0].lines[3], "+added");
}

TEST(diff_windows_line_endings_stripped) {
    std::string input =
        "diff --git a/win.txt b/win.txt\r\n"
        "--- a/win.txt\r\n"
        "+++ b/win.txt\r\n"
        "@@ -1,1 +1,1 @@\r\n"
        "-old\r\n"
        "+new\r\n";

    auto diffs = git::parse_diff(input);
    ASSERT_EQ(diffs.size(), static_cast<size_t>(1));
    ASSERT_STREQ(diffs[0].filePath, "win.txt");
    ASSERT_EQ(diffs[0].hunks[0].lines.size(), static_cast<size_t>(2));
    // Verify no trailing \r
    ASSERT_STREQ(diffs[0].hunks[0].lines[0], "-old");
    ASSERT_STREQ(diffs[0].hunks[0].lines[1], "+new");
}

// ===========================================================================
// parse_branch_list tests
// ===========================================================================

TEST(branch_empty_output) {
    auto branches = git::parse_branch_list("");
    ASSERT_TRUE(branches.empty());
}

TEST(branch_single_current) {
    std::string input = "main|abc1234|*|origin/main|[ahead 1]\n";
    auto branches = git::parse_branch_list(input);
    ASSERT_EQ(branches.size(), static_cast<size_t>(1));
    ASSERT_STREQ(branches[0].name, "main");
    ASSERT_STREQ(branches[0].shortHash, "abc1234");
    ASSERT_TRUE(branches[0].isCurrent);
    ASSERT_TRUE(branches[0].isLocal);
    ASSERT_STREQ(branches[0].upstream, "origin/main");
    ASSERT_STREQ(branches[0].tracking, "[ahead 1]");
}

TEST(branch_non_current) {
    std::string input = "feature|def5678| |origin/feature|\n";
    auto branches = git::parse_branch_list(input);
    ASSERT_EQ(branches.size(), static_cast<size_t>(1));
    ASSERT_STREQ(branches[0].name, "feature");
    ASSERT_FALSE(branches[0].isCurrent);
}

TEST(branch_multiple_sorted) {
    // Current branch should sort first, then alphabetical
    std::string input =
        "zebra|111| | |\n"
        "main|222|*|origin/main|\n"
        "alpha|333| | |\n";

    auto branches = git::parse_branch_list(input);
    ASSERT_EQ(branches.size(), static_cast<size_t>(3));
    // main is current, should be first
    ASSERT_STREQ(branches[0].name, "main");
    ASSERT_TRUE(branches[0].isCurrent);
    // Then alphabetical
    ASSERT_STREQ(branches[1].name, "alpha");
    ASSERT_STREQ(branches[2].name, "zebra");
}

TEST(branch_detached_head_skipped) {
    std::string input =
        "(HEAD detached at abc1234)|abc1234|*| |\n"
        "main|def5678| |origin/main|\n";

    auto branches = git::parse_branch_list(input);
    ASSERT_EQ(branches.size(), static_cast<size_t>(1));
    ASSERT_STREQ(branches[0].name, "main");
}

TEST(branch_no_upstream) {
    std::string input = "local-only|aaa111| ||\n";
    auto branches = git::parse_branch_list(input);
    ASSERT_EQ(branches.size(), static_cast<size_t>(1));
    ASSERT_TRUE(branches[0].upstream.empty());
    ASSERT_TRUE(branches[0].tracking.empty());
}

TEST(branch_minimal_fields) {
    // Only 3 pipe-separated fields (minimum required)
    std::string input = "mybranch|abc123| \n";
    auto branches = git::parse_branch_list(input);
    ASSERT_EQ(branches.size(), static_cast<size_t>(1));
    ASSERT_STREQ(branches[0].name, "mybranch");
    ASSERT_FALSE(branches[0].isCurrent);
}

TEST(branch_blank_lines_ignored) {
    std::string input =
        "\n"
        "main|abc|*|origin/main|\n"
        "\n";
    auto branches = git::parse_branch_list(input);
    ASSERT_EQ(branches.size(), static_cast<size_t>(1));
}

// ===========================================================================

int main() {
    printf("=== git_parser tests ===\n");
    RUN_ALL_TESTS();
}
