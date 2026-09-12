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

TEST(document_collection_retains_two_reviews_and_three_sources) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review("first", "one.cpp"));
    auto first = repo.workspace().active_id();
    navigation::open(repo, reading::review("second", "two.cpp"));
    auto second = repo.workspace().active_id();
    std::vector<reading::DocumentId> sources;
    for (const auto& path : {"a.cpp", "b.cpp", "c.cpp"}) {
        navigation::open(repo, reading::source(path, "second", 12));
        sources.push_back(repo.workspace().active_id());
    }
    ASSERT_EQ(repo.workspace().documents().size(), 6u);
    navigation::activate(repo, first);
    ASSERT_EQ(repo.selectedCommitHash(), "first");
    ASSERT_EQ(repo.selectedFilePath(), "one.cpp");
    navigation::activate(repo, second);
    ASSERT_EQ(repo.selectedCommitHash(), "second");
    ASSERT_EQ(repo.selectedFilePath(), "two.cpp");
    for (size_t i = 0; i < sources.size(); ++i) {
        navigation::activate(repo, sources[i]);
        ASSERT_EQ(repo.fullFilePath(), std::string(1, static_cast<char>('a' + i)) + ".cpp");
        ASSERT_EQ(repo.fullFileTargetLine(), 12);
        ASSERT_EQ(repo.fullFileRevision(), "second");
    }
    ASSERT_EQ(repo.workspace().documents().size(), 6u);
    navigation::return_to_review(repo);
    ASSERT_EQ(repo.workspace().active_id(), second);
}

TEST(document_identity_excludes_reading_location_and_origin) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review("commit", "a.cpp"));
    auto review = repo.workspace().active_id();
    navigation::open(repo, reading::review("commit", "b.cpp"));
    ASSERT_EQ(repo.workspace().active_id(), review);
    navigation::open(repo, reading::source("a.cpp", "commit", 12));
    auto source = repo.workspace().active_id();
    navigation::open(repo, reading::source("a.cpp", "commit", 99, reading::review("another")));
    ASSERT_EQ(repo.workspace().active_id(), source);
    ASSERT_EQ(repo.fullFileTargetLine(), 99);
    ASSERT_EQ(repo.workspace().documents().size(), 3u);
    navigation::open(repo, reading::source("a.cpp", "INDEX"));
    ASSERT_FALSE(repo.workspace().active_id() == source);
}

TEST(inactive_documents_release_rendering_payloads_and_keep_file_summaries) {
    ecs::RepoComponent repo;
    ecs::CommitDetailCache cache;
    navigation::open(repo, reading::review("commit"));
    cache.cachedCommitHash = "commit";
    cache.commitDetailBody = std::string(10000, 'x');
    ecs::FileDiff file;
    file.filePath = "file.cpp";
    file.additions = 7;
    file.hunks.push_back({});
    file.hunks.back().lines.push_back(std::string(10000, 'y'));
    cache.commitDetailDiff.push_back(file);
    navigation::remember_review_files(repo, cache.commitDetailDiff);
    navigation::open(repo, reading::source("file.cpp", "commit"));
    navigation::release_inactive_review(repo, cache);
    ASSERT_TRUE(cache.commitDetailDiff.empty());
    ASSERT_EQ(cache.commitDetailDiff.capacity(), 0u);
    ASSERT_TRUE(cache.commitDetailBody.empty());
    ASSERT_TRUE(cache.commitDetailBody.capacity() < 10000);
    ASSERT_EQ(repo.originFileSummaries.size(), 1u);
    ASSERT_EQ(repo.originFileSummaries[0].additions, 7);
    ASSERT_TRUE(repo.originFileSummaries[0].hunks.empty());
    repo.fullFileBytes = std::string(10000, 'z');
    repo.fullFileDiff.push_back(file);
    navigation::return_to_review(repo);
    ASSERT_TRUE(repo.fullFileBytes.empty());
    ASSERT_TRUE(repo.fullFileBytes.capacity() < 10000);
    ASSERT_EQ(repo.fullFileDiff.capacity(), 0u);
    ASSERT_TRUE(repo.workspace().document(repo.workspace().review())->files.has_value());
}

TEST(comparison_tab_reloads_after_another_review_releases_its_payload) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review("compare:base:target"));
    auto comparison = repo.workspace().active_id();
    repo.comparisonLoadedScope = repo.comparisonScope();
    repo.comparisonNeedsLoad = false;
    repo.comparisonDiff.resize(100);
    navigation::open(repo, reading::review("another"));
    ASSERT_EQ(repo.comparisonDiff.capacity(), 0u);
    ASSERT_TRUE(repo.comparisonLoadedScope.empty());
    navigation::activate(repo, comparison);
    ASSERT_TRUE(repo.comparisonNeedsLoad);
}

TEST(resolved_aliases_reuse_existing_source_review_and_comparison_documents) {
    const std::string oid(40, 'a'), parent(40, 'b');
    for (int kind = 0; kind < 3; ++kind) {
        ecs::RepoComponent repo;
        if (kind == 0) navigation::open(repo, reading::source("file.cpp", oid));
        if (kind == 1) navigation::open(repo, reading::review(oid));
        if (kind == 2) navigation::open(repo, reading::review("compare:" + parent + ":" + oid));
        auto existing = repo.workspace().active_id();
        if (kind == 0) navigation::open(repo, reading::source("file.cpp", "main", 17));
        if (kind == 1) navigation::open(repo, reading::review("main", "file.cpp"));
        if (kind == 2) navigation::open(repo, reading::review("compare:base:main", "file.cpp"));
        ASSERT_EQ(repo.workspace().documents().size(), 3u);
        auto request = navigation::stamp(repo, kind == 2 ? navigation::comparison_request_key(repo) : "request");
        if (kind == 0) ASSERT_TRUE(navigation::resolve_source(repo, request, oid));
        if (kind == 1) ASSERT_TRUE(navigation::resolve_review(repo, request, oid, parent));
        if (kind == 2) ASSERT_TRUE(navigation::complete_comparison(repo, request, parent, oid));
        ASSERT_EQ(repo.workspace().active_id(), existing);
        ASSERT_EQ(repo.workspace().documents().size(), 2u);
        navigation::open(repo, reading::review("wt"));
        navigation::activate(repo, existing);
        ASSERT_EQ(repo.workspace().active_id(), existing);
        if (kind == 0) ASSERT_EQ(repo.fullFileTargetLine(), 17);
        else ASSERT_EQ(repo.selectedFilePath(), "file.cpp");
    }
}

TEST(inactive_review_summaries_preserve_filters_rename_comments_and_progress) {
    ecs::RepoComponent repo;
    ecs::ReviewComponent review;
    navigation::open(repo, reading::review("commit"));
    std::vector<ecs::FileDiff> files;
    for (char change : {'A', 'D', 'R', 'M'}) {
        ecs::FileDiff file;
        file.filePath = std::string(1, change) + ".cpp";
        file.isNew = change == 'A';
        file.isDeleted = change == 'D';
        file.isRenamed = change == 'R';
        if (file.isRenamed) file.oldPath = "old.cpp";
        file.hunks.push_back({});
        file.hunks.back().lines = {"+line"};
        review.approvedHunks.insert("commit\n" + ecs::ReviewComponent::hunk_key(file.filePath, file.hunks[0]));
        review.reviewedFiles["commit\n" + file.filePath] = ecs::diff_signature(file);
        files.push_back(file);
    }
    ecs::ReviewComponent::Comment comment;
    comment.scope = "commit";
    comment.file = "old.cpp";
    comment.oldSide = true;
    review.comments.push_back(comment);
    navigation::remember_review_files(repo, files);
    navigation::open(repo, reading::source("M.cpp", "commit"));
    const auto& summaries = *repo.workspace().document(repo.workspace().review())->files;
    ASSERT_EQ(ecs::review_progress(review, "commit", summaries).reviewed, 4u);
    for (char change : {'A', 'D', 'R', 'M'}) {
        review_files::Filter filter;
        filter.change = change;
        auto visible = ecs::visible_file_indices(repo.originFileSummaries, filter);
        ASSERT_EQ(visible.size(), 1u);
        ASSERT_EQ(repo.originFileSummaries[visible[0]].filePath, std::string(1, change) + ".cpp");
    }
    review_files::Filter unresolved;
    unresolved.onlyUnresolved = true;
    auto visible = ecs::visible_review_file_indices(repo.originFileSummaries, unresolved, &review, "commit");
    ASSERT_EQ(visible.size(), 1u);
    ASSERT_EQ(repo.originFileSummaries[visible[0]].oldPath, "old.cpp");
    review.reviewedFiles.erase("commit\nR.cpp");
    ASSERT_EQ(ecs::review_progress(review, "commit", summaries).reviewed,
              ecs::review_progress(review, "commit", files).reviewed);
}

TEST(resolving_reviews_updates_retained_source_origins_and_history) {
    const std::string oid(40, 'a'), parent(40, 'b');
    for (bool comparison : {false, true}) {
        ecs::RepoComponent repo;
        navigation::open(repo, reading::review(comparison ? "compare:base:main" : "main", "original.cpp"));
        auto review = repo.workspace().active_id();
        navigation::open(repo, reading::source("file.cpp", oid));
        auto source = repo.workspace().active_id();
        navigation::activate(repo, review);
        auto request = navigation::stamp(repo, comparison ? navigation::comparison_request_key(repo) : "request");
        if (comparison) ASSERT_TRUE(navigation::complete_comparison(repo, request, parent, oid));
        else ASSERT_TRUE(navigation::resolve_review(repo, request, oid, parent));
        auto resolved = repo.workspace().review().destination;
        navigation::step(repo, -1);
        ASSERT_EQ(repo.workspace().source()->origin->destination, resolved);
        ASSERT_EQ(repo.workspace().source()->origin->file, "original.cpp");
        navigation::return_to_review(repo);
        ASSERT_EQ(repo.workspace().active_id(), review);
        navigation::activate(repo, source);
        ASSERT_EQ(repo.workspace().source()->origin->destination, resolved);
        navigation::return_to_review(repo);
        ASSERT_EQ(repo.workspace().documents().size(), 3u);
    }
}

TEST(document_close_reopens_reading_location_and_keeps_origin_summaries) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review("commit", "original.cpp"));
    auto review = repo.workspace().active_id();
    ecs::FileDiff file;
    file.filePath = "original.cpp";
    file.hunks.push_back({});
    file.hunks[0].lines = {"+line"};
    navigation::remember_review_files(repo, {file});
    navigation::open(repo, reading::source("original.cpp", "commit", 42));
    auto source = repo.workspace().active_id();
    navigation::activate(repo, review);
    navigation::close(repo, review);
    ASSERT_EQ(repo.workspace().active_id(), source);
    ASSERT_EQ(repo.originFileSummaries.size(), 1u);
    navigation::return_to_review(repo);
    auto recreated = repo.workspace().active_id();
    navigation::reopen_closed(repo);
    ASSERT_EQ(repo.workspace().active_id(), recreated);
    ASSERT_EQ(repo.workspace().document(recreated)->files->size(), 1u);
    navigation::close(repo, source);
    ASSERT_EQ(repo.workspace().active_id(), recreated);
    navigation::reopen_closed(repo);
    ASSERT_EQ(repo.fullFileTargetLine(), 42);
}

TEST(document_close_history_is_bounded_and_final_fallback_does_not_fill_it) {
    ecs::RepoComponent repo;
    for (int i = 0; i < 25; ++i) {
        navigation::open(repo, reading::source(std::to_string(i), ""));
        navigation::close(repo, repo.workspace().active_id());
    }
    ASSERT_EQ(repo.workspace().closed().size(), 20u);
    ASSERT_EQ(repo.workspace().documents().size(), 1u);
    auto lastClosed = repo.workspace().closed().back().id;
    auto fallback = repo.workspace().active_id();
    navigation::close(repo, fallback);
    ASSERT_EQ(repo.workspace().active_id(), fallback);
    ASSERT_EQ(repo.workspace().closed().back().id, lastClosed);
    navigation::reopen_closed(repo);
    ASSERT_EQ(repo.workspace().active_id(), lastClosed);
}

TEST(closed_source_origins_resolve_with_their_review) {
    ecs::RepoComponent repo;
    const std::string oid(40, 'a');
    navigation::open(repo, reading::review("main"));
    auto review = repo.workspace().active_id();
    navigation::open(repo, reading::source("source.cpp", oid));
    navigation::close(repo, repo.workspace().active_id());
    navigation::activate(repo, review);
    auto stamp = navigation::stamp(repo, "request");
    ASSERT_TRUE(navigation::resolve_review(repo, stamp, oid, ""));
    navigation::reopen_closed(repo);
    ASSERT_EQ(repo.workspace().source()->origin->destination, reading::review(oid).destination);
}

int main() { RUN_ALL_TESTS(); }
