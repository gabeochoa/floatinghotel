#include "test_framework.h"

#include "../../src/util/refresh_scope.h"

TEST(refresh_scope_classifies_worktree_paths) {
    auto scope = refresh_scope::classify("/tmp/repo", "/tmp/repo/.git", "/tmp/repo/src/a.cpp", false);
    ASSERT_TRUE(refresh_scope::has(scope, refresh_scope::Scope::Worktree));
    ASSERT_STREQ(refresh_scope::name(scope), "worktree");
}

TEST(refresh_scope_classifies_git_index_paths) {
    auto scope = refresh_scope::classify("/tmp/repo", "/tmp/repo/.git", "/tmp/repo/.git/index", false);
    ASSERT_TRUE(refresh_scope::has(scope, refresh_scope::Scope::Index));
    auto plan = refresh_scope::plan(scope);
    ASSERT_TRUE(plan.status);
    ASSERT_TRUE(plan.diff);
    ASSERT_TRUE(plan.stagedDiff);
    ASSERT_TRUE(plan.files);
    ASSERT_FALSE(plan.log);
    ASSERT_FALSE(plan.branches);
}

TEST(refresh_scope_classifies_common_ref_paths) {
    auto scope = refresh_scope::classify("/tmp/repo/work", "/tmp/repo/.git", "/tmp/repo/.git/refs/heads/main", false);
    ASSERT_TRUE(refresh_scope::has(scope, refresh_scope::Scope::Refs));
    auto plan = refresh_scope::plan(scope);
    ASSERT_TRUE(plan.status);
    ASSERT_TRUE(plan.log);
    ASSERT_TRUE(plan.stagedDiff);
    ASSERT_TRUE(plan.branches);
    ASSERT_FALSE(plan.diff);
}

TEST(refresh_scope_dropped_events_are_full) {
    auto scope = refresh_scope::classify("/tmp/repo", "/tmp/repo/.git", "/tmp/repo/src/a.cpp", true);
    ASSERT_EQ(scope, refresh_scope::Scope::Full);
    auto plan = refresh_scope::plan(scope);
    ASSERT_TRUE(plan.status);
    ASSERT_TRUE(plan.log);
    ASSERT_TRUE(plan.diff);
    ASSERT_TRUE(plan.stagedDiff);
    ASSERT_TRUE(plan.branches);
    ASSERT_TRUE(plan.files);
}

TEST(refresh_scope_merges_without_losing_full) {
    auto scope = refresh_scope::merge(refresh_scope::Scope::Worktree, refresh_scope::Scope::Index);
    ASSERT_STREQ(refresh_scope::name(scope), "worktree+index");
    ASSERT_EQ(refresh_scope::merge(scope, refresh_scope::Scope::Full), refresh_scope::Scope::Full);
}

int main() {
    printf("=== refresh scope tests ===\n");
    RUN_ALL_TESTS();
}
