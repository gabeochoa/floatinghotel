#include "test_framework.h"
#include "../../src/util/repo_picker.h"

using repo_picker::Section;

TEST(repo_picker_groups_worktrees_and_dedupes_on_main_repo) {
    std::map<std::string, repo_picker::Resolved> resolved{
        {"/home/u/main", {"/home/u/main", "/home/u/main", true}},
        {"/home/u/main-wt", {"/home/u/main-wt", "/home/u/main", true}},
        {"/home/u/other", {"/home/u/other", "/home/u/other", true}},
    };
    auto entries = repo_picker::build(
        {{"/home/u/main-wt", "feature", true, true}},
        {"/home/u/other"},
        {"/home/u/main", "/home/u/main-wt", "/home/u/other"},
        resolved);
    ASSERT_EQ(entries.size(), size_t{2});
    // The open worktree tab represents its main-repo group.
    ASSERT_EQ(entries[0].path, std::string("/home/u/main-wt"));
    ASSERT_EQ(entries[0].section, Section::Open);
    ASSERT_EQ(entries[0].members.size(), size_t{2});
    ASSERT_TRUE(entries[0].dirty);
    ASSERT_EQ(entries[1].path, std::string("/home/u/other"));
    ASSERT_EQ(entries[1].section, Section::Pinned);
    ASSERT_TRUE(entries[1].pinned);
}

TEST(repo_picker_missing_folders_land_in_missing_section) {
    std::map<std::string, repo_picker::Resolved> resolved{
        {"/gone", {"/gone", "/gone", false}},
        {"/here", {"/here", "/here", true}},
    };
    auto entries = repo_picker::build({}, {}, {"/gone", "/here"}, resolved);
    ASSERT_EQ(entries.size(), size_t{2});
    ASSERT_EQ(entries[0].path, std::string("/here"));
    ASSERT_EQ(entries[1].section, Section::Missing);
    ASSERT_FALSE(entries[1].exists);
    // Missing entries rank last even when they match best.
    auto order = repo_picker::rank(entries, "gone", "");
    ASSERT_EQ(order.size(), size_t{1});
    ASSERT_EQ(order[0], size_t{1});
}

TEST(repo_picker_recency_order_on_empty_query_and_fuzzy_on_shortened_path) {
    auto entries = repo_picker::build({}, {}, {"/home/u/alpha", "/home/u/beta"}, {});
    auto empty = repo_picker::rank(entries, "", "/home/u");
    ASSERT_EQ(empty[0], size_t{0});
    ASSERT_EQ(empty[1], size_t{1});
    auto filtered = repo_picker::rank(entries, "~/beta", "/home/u");
    ASSERT_EQ(filtered.size(), size_t{1});
    ASSERT_EQ(filtered[0], size_t{1});
    ASSERT_EQ(repo_picker::shorten_home("/home/u/x", "/home/u"), std::string("~/x"));
    ASSERT_EQ(repo_picker::normalize("/a/b/./"), std::string("/a/b"));
}

int main() { RUN_ALL_TESTS(); }
