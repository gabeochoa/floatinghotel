// Unit tests for git::parse_status, git::parse_log, git::parse_diff,
// git::parse_branch_list.
//
// Each test feeds realistic git-format strings to the parser and verifies
// the returned structs.

#include "test_framework.h"
#include "../../src/git/git_parser.h"

#include <string>

TEST(diff_file_modes_preserve_permission_and_creation_changes) {
    auto modes = git::parse_diff("diff --git a/run.sh b/run.sh\nold mode 100644\nnew mode 100755\n");
    ASSERT_EQ(modes.size(), 1u);
    ASSERT_EQ(modes[0].oldMode, "100644");
    ASSERT_EQ(modes[0].newMode, "100755");
    ASSERT_TRUE(modes[0].hunks.empty());
    auto created = git::parse_diff("diff --git a/new b/new\nnew file mode 100755\nindex 0000000..1234567\n");
    ASSERT_TRUE(created[0].isNew);
    ASSERT_EQ(created[0].newMode, "100755");
    ASSERT_TRUE(created[0].oldMode.empty());
    auto deleted = git::parse_diff("diff --git a/old b/old\ndeleted file mode 100644\nindex 1234567..0000000\n");
    ASSERT_TRUE(deleted[0].isDeleted);
    ASSERT_EQ(deleted[0].oldMode, "100644");
    ASSERT_TRUE(deleted[0].newMode.empty());
    auto same = git::parse_diff("diff --git a/a b/a\nindex 1234567..abcdef0 100644\n");
    ASSERT_EQ(same[0].oldMode, same[0].newMode);
}

TEST(diff_marks_unique_unchanged_blocks_as_moved) {
    auto diff = git::parse_diff("diff --git a/a.cpp b/a.cpp\n--- a/a.cpp\n+++ b/a.cpp\n"
        "@@ -1,3 +1 @@\n-long moved first line\n-long moved second line\n anchor\n"
        "@@ -8 +6,3 @@\n tail\n+long moved first line\n+long moved second line\n");
    ASSERT_EQ(diff[0].hunks[0].movedLines, (std::set<size_t>{0, 1}));
    ASSERT_EQ(diff[0].hunks[1].movedLines, (std::set<size_t>{1, 2}));
    auto changed = git::parse_diff("diff --git a/a.cpp b/a.cpp\n@@ -1,2 +1,2 @@\n"
        "-long moved first line\n-long moved second line\n+long moved first line\n+edited second line\n");
    ASSERT_TRUE(changed[0].hunks[0].movedLines.empty());
    auto ambiguous = git::parse_diff("diff --git a/a.cpp b/a.cpp\n@@ -1,4 +1,2 @@\n"
        "-long moved first line\n-long moved second line\n anchor\n"
        "-long moved first line\n-long moved second line\n+long moved first line\n+long moved second line\n");
    ASSERT_TRUE(ambiguous[0].hunks[0].movedLines.empty());
}

TEST(blame_line_preserves_original_location_and_author) {
    auto line = git::parse_blame_line(std::string(40, 'a') + " 3 9 1\nauthor Ada Lovelace\nsummary Initial algorithm\nfilename old name.cpp\n\treturn answer;\n");
    ASSERT_EQ(line.author, "Ada Lovelace");
    ASSERT_EQ(line.summary, "Initial algorithm");
    ASSERT_EQ(line.originalLine, 3);
    ASSERT_EQ(line.finalLine, 9);
    ASSERT_EQ(line.file, "old name.cpp");
    ASSERT_EQ(line.content, "return answer;");
    ASSERT_TRUE(git::parse_blame_line("malformed output").hash.empty());
}

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

TEST(status_submodule_pointer_change) {
    // porcelain v2 'sub' field "SC.." => submodule with a changed commit.
    // (real output from `git status --porcelain=v2` on a moved gitlink)
    std::string input =
        "1 .M SC.. 160000 160000 160000 "
        "4571d62169cf921c00ad504b334c09c24aced554 "
        "4571d62169cf921c00ad504b334c09c24aced554 vendored\n";
    auto r = git::parse_status(input);
    ASSERT_EQ(r.unstagedFiles.size(), static_cast<size_t>(1));
    ASSERT_STREQ(r.unstagedFiles[0].path, "vendored");
    ASSERT_TRUE(r.unstagedFiles[0].isSubmodule);
}

TEST(status_ordinary_file_not_submodule) {
    std::string input =
        "1 .M N... 100644 100644 100644 "
        "abc1234abc1234abc1234abc1234abc1234abc1234 "
        "def5678def5678def5678def5678def5678def5678 README.md\n";
    auto r = git::parse_status(input);
    ASSERT_EQ(r.unstagedFiles.size(), static_cast<size_t>(1));
    ASSERT_FALSE(r.unstagedFiles[0].isSubmodule);
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

TEST(diff_submodule_pointer_change) {
    // Default `git diff` for a gitlink: index line ends in 160000, hunk holds
    // the "Subproject commit" lines. (real output from the fixture)
    std::string input =
        "diff --git a/vendored b/vendored\n"
        "index 4571d62..fe41444 160000\n"
        "--- a/vendored\n"
        "+++ b/vendored\n"
        "@@ -1 +1 @@\n"
        "-Subproject commit 4571d62169cf921c00ad504b334c09c24aced554\n"
        "+Subproject commit fe41444737ac27577c5f2ce700df2cabff52d1df\n";
    auto diffs = git::parse_diff(input);
    ASSERT_EQ(diffs.size(), static_cast<size_t>(1));
    ASSERT_STREQ(diffs[0].filePath, "vendored");
    ASSERT_TRUE(diffs[0].isSubmodule);
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

TEST(diff_preserves_crlf_content_and_missing_newline) {
    auto diffs = git::parse_diff("diff --git a/file b/file\n--- a/file\n+++ b/file\n@@ -1 +1 @@\n-old\r\n+new\n\\ No newline at end of file\n");
    ASSERT_EQ(diffs[0].hunks[0].lines[0], "-old\r");
    ASSERT_TRUE(diffs[0].hunks[0].noNewline.contains(1));
}

TEST(null_paths_preserve_spaces_and_tabs) {
    std::string output = std::string("space name") + '\0' + "tab\tname" + '\0' + "space name" + '\0';
    auto paths = git::parse_null_paths(output);
    ASSERT_EQ(paths.size(), 2u);
    ASSERT_EQ(paths[0], "space name");
    ASSERT_EQ(paths[1], "tab\tname");
}

TEST(grep_matches_parse_nul_separated_locations) {
    std::string output = std::string("dir/file name.cpp") + '\0' + "42" + '\0' + "needle: value\n";
    auto matches = git::parse_grep_matches(output);
    ASSERT_EQ(matches.size(), 1u);
    ASSERT_EQ(matches[0].file, "dir/file name.cpp");
    ASSERT_EQ(matches[0].line, 42);
    ASSERT_EQ(matches[0].text, "needle: value");
    ASSERT_TRUE(git::parse_grep_matches("malformed").empty());
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

TEST(binary_diff_object_ids_keep_the_full_hash_without_a_mode_field) {
    std::string before(64, 'a'), after(64, 'b');
    auto patch = "diff --git a/image.bin b/image.bin\nindex " + before + ".." + after +
        "\nBinary files a/image.bin and b/image.bin differ\n";
    auto files = git::parse_diff(patch);
    ASSERT_EQ(files.size(), size_t{1});
    ASSERT_EQ(files.front().oldObject, before);
    ASSERT_EQ(files.front().newObject, after);
    ASSERT_TRUE(files.front().isBinary);
    auto withMode = git::parse_diff("diff --git a/image.bin b/image.bin\nindex abc..def 100644\nBinary files a/image.bin and b/image.bin differ\n");
    ASSERT_EQ(withMode.front().oldObject, "abc");
    ASSERT_EQ(withMode.front().newObject, "def");
}

int main() {
    printf("=== git_parser tests ===\n");
    RUN_ALL_TESTS();
}
