#include "test_framework.h"
#include "../../src/ecs/components.h"

TEST(comparison_target_jump_discards_the_previous_pending_result) {
    ecs::RepoComponent repo;
    repo.comparisonScope = "compare:old-base:old-target";
    repo.comparisonError = "Old failure";
    std::promise<git::RevisionComparison> previous;
    repo.comparisonFuture = async_work::Task<git::RevisionComparison>(
        previous.get_future(), std::stop_source{});
    ecs::select_review_target(repo, "compare:new-base:new-target", "file.cpp");
    ASSERT_FALSE(repo.comparisonFuture.valid());
    ASSERT_TRUE(repo.comparisonNeedsLoad);
    ASSERT_TRUE(repo.comparisonError.empty());
    ASSERT_EQ(repo.comparisonBase, "new-base");
    ASSERT_EQ(repo.comparisonTarget, "new-target");
}

TEST(staged_review_draft_restores_its_file_and_index_side) {
    ecs::ReviewComponent review;
    ecs::begin_comment(review, "index-draft", {"index", "file.cpp", 1, "Check staged change"});
    ecs::RepoComponent repo;
    ecs::restore_draft_selection(repo, review);
    ASSERT_EQ(repo.selectedFilePath, "file.cpp");
    ASSERT_TRUE(repo.selectedFileStaged);
    ecs::select_review_target(repo, "wt", "other.cpp");
    ASSERT_FALSE(repo.selectedFileStaged);
}

TEST(comparison_loading_does_not_restore_an_unrelated_branch_review) {
    ecs::RepoComponent repo;
    repo.repoPath = "repo";
    repo.currentBranch = "main";
    repo.headCommitHash = "head";
    ecs::ReviewComponent review;
    review.storageRepoPath = repo.repoPath;
    review.storageScope = "compare:old-base:old-target";
    repo.comparisonOpen = true;
    ASSERT_EQ(ecs::selected_review_storage_scope(repo, review), review.storageScope);
    repo.comparisonScope = "compare:new-base:new-target";
    ASSERT_EQ(ecs::selected_review_storage_scope(repo, review), repo.comparisonScope);
    repo.comparisonOpen = false;
    ASSERT_EQ(ecs::selected_review_storage_scope(repo, review), ecs::review_scope(repo));
    repo.comparisonOpen = true;
    repo.comparisonScope.clear();
    repo.repoPath = "other-repo";
    ASSERT_EQ(ecs::selected_review_storage_scope(repo, review), ecs::review_scope(repo));
}

TEST(saved_review_target_exits_series_mode_and_discards_its_pending_result) {
    ecs::RepoComponent repo;
    repo.rangeDiff.enabled = true;
    std::promise<git::GitResult> previous;
    repo.rangeDiff.future = async_work::Task<git::GitResult>(
        previous.get_future(), std::stop_source{});
    ecs::select_review_target(repo, "compare:base:target", "file.cpp");
    ASSERT_FALSE(repo.rangeDiff.enabled);
    ASSERT_FALSE(repo.rangeDiff.future.valid());
    ASSERT_TRUE(repo.comparisonOpen);
}

int main() { RUN_ALL_TESTS(); }
