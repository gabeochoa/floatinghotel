#include "test_framework.h"
#include "../../src/util/navigation.h"
#include "../../src/ecs/components.h"

TEST(comparison_target_jump_discards_the_previous_pending_result) {
    ecs::RepoComponent repo;
    repo.comparisonLoadedScope = "compare:old-base:old-target";
    repo.comparisonError = "Old failure";
    std::promise<git::RevisionComparison> previous;
    repo.comparisonFuture = async_work::Task<git::RevisionComparison>(
        previous.get_future(), std::stop_source{});
    navigation::open(repo, reading::review("compare:new-base:new-target", "file.cpp"));
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
    navigation::restore_draft(repo, review);
    ASSERT_EQ(repo.selectedFilePath(), "file.cpp");
    ASSERT_TRUE(repo.selectedFileStaged());
    navigation::open(repo, reading::review("wt", "other.cpp"));
    ASSERT_FALSE(repo.selectedFileStaged());
}

TEST(comparison_loading_does_not_restore_an_unrelated_branch_review) {
    ecs::RepoComponent repo;
    repo.repoPath = "repo";
    repo.currentBranch = "main";
    repo.headCommitHash = "head";
    ecs::ReviewComponent review;
    review.storageRepoPath = repo.repoPath;
    review.storageScope = "compare:old-base:old-target";
    repo.comparisonEditorOpen = true;
    ASSERT_EQ(ecs::selected_review_storage_scope(repo, review), review.storageScope);
    navigation::open(repo, reading::review("compare:new-base:new-target"));
    ASSERT_EQ(ecs::selected_review_storage_scope(repo, review), repo.comparisonScope());
    navigation::open(repo, reading::review("wt"));
    ASSERT_EQ(ecs::selected_review_storage_scope(repo, review), ecs::review_scope(repo));
    repo.comparisonEditorOpen = true;
    repo.repoPath = "other-repo";
    ASSERT_EQ(ecs::selected_review_storage_scope(repo, review), ecs::review_scope(repo));
}

TEST(saved_review_target_exits_series_mode_and_discards_its_pending_result) {
    ecs::RepoComponent repo;
    repo.rangeDiff.enabled = true;
    std::promise<git::GitResult> previous;
    repo.rangeDiff.future = async_work::Task<git::GitResult>(
        previous.get_future(), std::stop_source{});
    navigation::open(repo, reading::review("compare:base:target", "file.cpp"));
    ASSERT_FALSE(repo.rangeDiff.enabled);
    ASSERT_FALSE(repo.rangeDiff.future.valid());
    ASSERT_TRUE(repo.comparisonOpen());
}

TEST(navigation_result_stamp_rejects_each_superseded_identity) {
    ecs::RepoComponent repo;
    repo.repoPath = "repo";
    navigation::open(repo, reading::source("first.cpp", "main"));
    auto first = navigation::stamp(repo, "page:1");
    ASSERT_TRUE(navigation::accepts(repo, first, "page:1"));
    ASSERT_FALSE(navigation::accepts(repo, first, "page:2"));
    repo.repoPath = "other";
    ASSERT_FALSE(navigation::accepts(repo, first, "page:1"));
    repo.repoPath = "repo";
    ++repo.dataGeneration;
    ASSERT_FALSE(navigation::accepts(repo, first, "page:1"));
    --repo.dataGeneration;
    navigation::open(repo, reading::source("second.cpp", "main"));
    ASSERT_FALSE(navigation::accepts(repo, first, "page:1"));
    navigation::step(repo, -1);
    ASSERT_EQ(repo.fullFilePath(), "first.cpp");
    ASSERT_FALSE(navigation::accepts(repo, first, "page:1"));
    auto current = navigation::stamp(repo, "page:1");
    navigation::reset(repo);
    navigation::open(repo, reading::source("first.cpp", "main"));
    ASSERT_FALSE(navigation::accepts(repo, current, "page:1"));
}

TEST(historical_resolution_pins_history_without_creating_a_visit) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review("origin", "old.cpp"));
    navigation::open(repo, reading::source("old.cpp", "main", 12));
    auto request = navigation::stamp(repo, "page");
    const auto count = repo.workspace().history().size();
    std::string oid(40, 'a');
    ASSERT_TRUE(navigation::resolve_source(repo, request, oid));
    ASSERT_EQ(repo.workspace().history().size(), count);
    ASSERT_EQ(repo.fullFileRevision(), oid);
    navigation::open(repo, reading::source("working.cpp"));
    ASSERT_FALSE(navigation::resolve_source(repo, request, std::string(40, 'b')));
    navigation::step(repo, -1);
    ASSERT_EQ(repo.fullFileRevision(), oid);
    navigation::return_to_review(repo);
    ASSERT_EQ(repo.selectedCommitHash(), "origin");
    ASSERT_EQ(repo.selectedFilePath(), "old.cpp");
}

TEST(reselecting_the_same_destination_keeps_pending_work) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::source("same.cpp"));
    std::promise<ecs::FullFileContent> promise;
    std::stop_source stop;
    repo.fullFileFuture = {promise.get_future(), stop};
    auto generation = repo.workspace().generation();
    repo.navigationEffect.reset();
    navigation::open(repo, reading::source("same.cpp"));
    ASSERT_FALSE(repo.navigationEffect->changed);
    ASSERT_FALSE(repo.navigationEffect->dismissedPanel);
    ASSERT_EQ(repo.workspace().generation(), generation);
    ASSERT_TRUE(repo.fullFileFuture.valid());
    ASSERT_FALSE(stop.stop_requested());
    navigation::open(repo, reading::source("next.cpp"));
    ASSERT_TRUE(stop.stop_requested());
}

TEST(typed_destinations_separate_working_index_historical_and_review) {
    ASSERT_FALSE(reading::source("file") == reading::source("file", "INDEX"));
    ASSERT_FALSE(reading::source("file", "INDEX") == reading::source("file", std::string(40, 'a')));
    ASSERT_FALSE(reading::review("wt") == reading::review("index"));
    ASSERT_FALSE(reading::review("parent:a:b") == reading::review("compare:a:b"));
    ASSERT_EQ(reading::scope(reading::review("parent:a:b")), "parent:a:b");
}

TEST(comparison_resolution_updates_the_same_visit_and_ignores_late_results) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review("compare:main:topic", "changed.cpp"));
    auto count = repo.workspace().history().size();
    auto request = navigation::stamp(repo, navigation::comparison_request_key(repo));
    std::string base(40, 'a'), target(40, 'b');
    ASSERT_TRUE(navigation::complete_comparison(repo, request, base, target));
    ASSERT_EQ(repo.workspace().history().size(), count);
    ASSERT_EQ(repo.comparisonScope(), "compare:" + base + ":" + target);
    ASSERT_EQ(repo.selectedFilePath(), "changed.cpp");
    navigation::open(repo, reading::review("wt"));
    ASSERT_FALSE(navigation::complete_comparison(repo, request, base, target));
    ASSERT_FALSE(repo.comparisonOpen());
}

TEST(opening_source_keeps_the_retained_comparisons_review_storage) {
    ecs::RepoComponent repo;
    ecs::ReviewComponent review;
    repo.repoPath = "repo";
    navigation::open(repo, reading::review("compare:base:target"));
    auto scope = ecs::selected_review_storage_scope(repo, review);
    navigation::open(repo, reading::source("changed.cpp", "target"));
    ASSERT_FALSE(repo.comparisonOpen());
    ASSERT_EQ(ecs::selected_review_storage_scope(repo, review), scope);
    navigation::return_to_review(repo);
    ASSERT_TRUE(repo.comparisonOpen());
}

TEST(consuming_a_source_reveal_does_not_restore_a_stale_line_on_activation) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::source("file.cpp", "", 42));
    auto count = repo.workspace().history().size();
    navigation::clear_source_reveal(repo);
    ASSERT_EQ(repo.fullFileTargetLine(), 0);
    ASSERT_EQ(repo.workspace().history().size(), count);
    navigation::activate(repo, reading::Slot::Review);
    navigation::activate(repo, reading::Slot::Source);
    ASSERT_EQ(repo.fullFileTargetLine(), 0);
}

TEST(comparison_results_from_old_diff_options_are_not_applied) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review("compare:main:topic"));
    auto request = navigation::stamp(repo, navigation::comparison_request_key(repo));
    repo.diffContext = 20;
    ASSERT_FALSE(navigation::complete_comparison(repo, request, std::string(40, 'a'), std::string(40, 'b')));
    ASSERT_EQ(repo.comparisonScope(), "compare:main:topic");
    ASSERT_TRUE(repo.comparisonNeedsLoad);
}

TEST(submitted_comparison_rejects_edited_fields_and_merge_mode) {
    ecs::RepoComponent repo;
    navigation::comparison_editor(repo);
    repo.comparisonRequest = ecs::RepoComponent::ComparisonRequest::SubmittedForm;
    repo.comparisonBase = "main";
    repo.comparisonTarget = "topic";
    auto request = navigation::stamp(repo, navigation::comparison_request_key(repo));
    std::string base(40, 'a'), target(40, 'b');
    repo.comparisonTarget = "other";
    ASSERT_FALSE(navigation::complete_comparison(repo, request, base, target));
    repo.comparisonTarget = "topic";
    repo.comparisonBase = "older";
    ASSERT_FALSE(navigation::complete_comparison(repo, request, base, target));
    repo.comparisonBase = "main";
    repo.comparisonMergeBase = true;
    ASSERT_FALSE(navigation::complete_comparison(repo, request, base, target));
    ASSERT_TRUE(repo.comparisonScope().empty());
    repo.comparisonMergeBase = false;
    ASSERT_TRUE(navigation::complete_comparison(repo, request, base, target));
    ASSERT_EQ(repo.comparisonScope(), "compare:" + base + ":" + target);
}

TEST(document_comparison_reload_ignores_unsubmitted_form_edits) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review("compare:main:topic"));
    auto request = navigation::stamp(repo, navigation::comparison_request_key(repo));
    repo.comparisonBase = "unsubmitted";
    repo.comparisonTarget = "edits";
    repo.comparisonMergeBase = true;
    ASSERT_TRUE(navigation::complete_comparison(repo, request, std::string(40, 'a'), std::string(40, 'b')));
}

int main() { RUN_ALL_TESTS(); }
