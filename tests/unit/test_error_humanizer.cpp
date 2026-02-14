// Unit tests for git::humanize_error.

#include "test_framework.h"
#include "../../src/git/error_humanizer.h"

TEST(humanize_auth_failed) {
    std::string raw = "fatal: Authentication failed for 'https://github.com/user/repo.git'";
    std::string result = git::humanize_error(raw);
    ASSERT_STREQ(result,
        "Git credentials not configured. Run 'git config credential.helper' to set up.");
}

TEST(humanize_could_not_read_username) {
    std::string raw = "fatal: could not read Username for 'https://github.com': terminal prompts disabled";
    std::string result = git::humanize_error(raw);
    ASSERT_STREQ(result,
        "Git credentials not configured. Set up a credential helper.");
}

TEST(humanize_conflict) {
    std::string raw = "CONFLICT (content): Merge conflict in src/main.cpp\nAutomatic merge failed";
    std::string result = git::humanize_error(raw);
    ASSERT_STREQ(result,
        "Merge conflict detected. Resolve conflicts before committing.");
}

TEST(humanize_non_fast_forward) {
    std::string raw = "! [rejected] main -> main (non-fast-forward)\nerror: failed to push";
    std::string result = git::humanize_error(raw);
    ASSERT_STREQ(result,
        "Remote has new commits. Pull before pushing.");
}

TEST(humanize_rejected) {
    std::string raw = "! [rejected] main -> main (fetch first)";
    std::string result = git::humanize_error(raw);
    ASSERT_STREQ(result,
        "Push rejected by remote. Pull latest changes first.");
}

TEST(humanize_detached_head) {
    std::string raw = "You are in 'detached HEAD' state.";
    std::string result = git::humanize_error(raw);
    ASSERT_STREQ(result,
        "You are in detached HEAD state. Create a branch to save your work.");
}

TEST(humanize_not_a_git_repository) {
    std::string raw = "fatal: not a git repository (or any of the parent directories): .git";
    std::string result = git::humanize_error(raw);
    ASSERT_STREQ(result,
        "This directory is not a git repository.");
}

TEST(humanize_pathspec) {
    std::string raw = "error: pathspec 'nonexistent.txt' did not match any file(s)";
    std::string result = git::humanize_error(raw);
    ASSERT_STREQ(result,
        "The specified file or path was not found.");
}

TEST(humanize_nothing_to_commit) {
    std::string raw = "On branch main\nnothing to commit, working tree clean";
    std::string result = git::humanize_error(raw);
    ASSERT_STREQ(result,
        "No changes to commit. Stage some changes first.");
}

TEST(humanize_branch_already_exists) {
    std::string raw = "fatal: a branch named 'feature' already exists";
    std::string result = git::humanize_error(raw);
    ASSERT_STREQ(result,
        "A branch with that name already exists.");
}

TEST(humanize_not_fully_merged) {
    std::string raw = "error: the branch 'feature' is not fully merged";
    std::string result = git::humanize_error(raw);
    ASSERT_STREQ(result,
        "Branch has unmerged changes. Use force delete (-D) to delete anyway.");
}

TEST(humanize_local_changes) {
    std::string raw = "error: Your local changes to the following files would be overwritten by checkout";
    std::string result = git::humanize_error(raw);
    ASSERT_STREQ(result,
        "You have uncommitted changes. Commit or stash them first.");
}

TEST(humanize_unknown_error_returns_raw) {
    std::string raw = "some completely unknown error message xyz123";
    std::string result = git::humanize_error(raw);
    // Should return the raw string unchanged
    ASSERT_STREQ(result, raw);
}

TEST(humanize_empty_string) {
    std::string result = git::humanize_error("");
    ASSERT_STREQ(result, "");
}

TEST(humanize_first_matching_pattern_wins) {
    // "rejected" appears in the patterns list. Make sure the first match is
    // returned, not a later one. "non-fast-forward" comes before "rejected"
    // in the pattern list, so if the input contains "non-fast-forward" it
    // should match that pattern, not "rejected".
    std::string raw = "! [rejected] main -> main (non-fast-forward)";
    std::string result = git::humanize_error(raw);
    // "non-fast-forward" is checked before "rejected", so it should match first.
    ASSERT_STREQ(result,
        "Remote has new commits. Pull before pushing.");
}

int main() {
    printf("=== error_humanizer tests ===\n");
    RUN_ALL_TESTS();
}
