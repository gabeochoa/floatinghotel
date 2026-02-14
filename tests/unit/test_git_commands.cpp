// Unit tests for git::build_patch -- the pure function that constructs
// a unified diff patch string from FileDiff + DiffHunk structs.

#include "test_framework.h"
#include "../../src/git/git_commands.h"

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

int main() {
    printf("=== git_commands tests ===\n");
    RUN_ALL_TESTS();
}
