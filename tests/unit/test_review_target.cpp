#include "test_framework.h"
#include "../../src/util/navigation.h"
#include "../../src/util/document_cycle.h"
#include "../../src/ecs/components.h"

static void open_kept(ecs::RepoComponent& repo, reading::Location location, std::optional<bool> reviewing = {}) {
    navigation::open(repo, std::move(location), reviewing, reading::OpenMode::Keep);
}

TEST(comparison_target_jump_discards_the_previous_pending_result) {
    ecs::RepoComponent repo;
    repo.comparisonLoadedScope = "compare:old-base:old-target";
    repo.comparisonError = "Old failure";
    std::promise<git::RevisionComparison> previous;
    repo.comparisonFuture = async_work::Task<git::RevisionComparison>(
        previous.get_future(), std::stop_source{});
    open_kept(repo, reading::review("compare:new-base:new-target", "file.cpp"));
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
    open_kept(repo, reading::review("wt", "other.cpp"));
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
    open_kept(repo, reading::review("compare:new-base:new-target"));
    ASSERT_EQ(ecs::selected_review_storage_scope(repo, review), repo.comparisonScope());
    open_kept(repo, reading::review("wt"));
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
    open_kept(repo, reading::review("compare:base:target", "file.cpp"));
    ASSERT_FALSE(repo.rangeDiff.enabled);
    ASSERT_FALSE(repo.rangeDiff.future.valid());
    ASSERT_TRUE(repo.comparisonOpen());
}

TEST(navigation_result_stamp_rejects_each_superseded_identity) {
    ecs::RepoComponent repo;
    repo.repoPath = "repo";
    open_kept(repo, reading::source("first.cpp", "main"));
    auto first = navigation::stamp(repo, "page:1");
    ASSERT_TRUE(navigation::accepts(repo, first, "page:1"));
    ASSERT_FALSE(navigation::accepts(repo, first, "page:2"));
    repo.repoPath = "other";
    ASSERT_FALSE(navigation::accepts(repo, first, "page:1"));
    repo.repoPath = "repo";
    ++repo.dataGeneration;
    ASSERT_FALSE(navigation::accepts(repo, first, "page:1"));
    --repo.dataGeneration;
    open_kept(repo, reading::source("second.cpp", "main"));
    ASSERT_FALSE(navigation::accepts(repo, first, "page:1"));
    navigation::step(repo, -1);
    ASSERT_EQ(repo.fullFilePath(), "first.cpp");
    ASSERT_FALSE(navigation::accepts(repo, first, "page:1"));
    auto current = navigation::stamp(repo, "page:1");
    navigation::reset(repo);
    open_kept(repo, reading::source("first.cpp", "main"));
    ASSERT_FALSE(navigation::accepts(repo, current, "page:1"));
}

TEST(historical_resolution_pins_history_without_creating_a_visit) {
    ecs::RepoComponent repo;
    open_kept(repo, reading::review("origin", "old.cpp"));
    open_kept(repo, reading::source("old.cpp", "main", 12));
    auto request = navigation::stamp(repo, "page");
    const auto count = repo.workspace().history().size();
    std::string oid(40, 'a');
    ASSERT_TRUE(navigation::resolve_source(repo, request, oid));
    ASSERT_EQ(repo.workspace().history().size(), count);
    ASSERT_EQ(repo.fullFileRevision(), oid);
    open_kept(repo, reading::source("working.cpp"));
    ASSERT_FALSE(navigation::resolve_source(repo, request, std::string(40, 'b')));
    navigation::step(repo, -1);
    ASSERT_EQ(repo.fullFileRevision(), oid);
    navigation::return_to_review(repo);
    ASSERT_EQ(repo.selectedCommitHash(), "origin");
    ASSERT_EQ(repo.selectedFilePath(), "old.cpp");
}

TEST(reselecting_the_same_destination_keeps_pending_work) {
    ecs::RepoComponent repo;
    open_kept(repo, reading::source("same.cpp"));
    std::promise<ecs::FullFileContent> promise;
    std::stop_source stop;
    repo.fullFileFuture = {promise.get_future(), stop};
    auto generation = repo.workspace().generation();
    repo.navigationEffect.reset();
    open_kept(repo, reading::source("same.cpp"));
    ASSERT_FALSE(repo.navigationEffect->changed);
    ASSERT_FALSE(repo.navigationEffect->dismissedPanel);
    ASSERT_EQ(repo.workspace().generation(), generation);
    ASSERT_TRUE(repo.fullFileFuture.valid());
    ASSERT_FALSE(stop.stop_requested());
    open_kept(repo, reading::source("next.cpp"));
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
    open_kept(repo, reading::review("compare:main:topic", "changed.cpp"));
    auto count = repo.workspace().history().size();
    auto request = navigation::stamp(repo, navigation::comparison_request_key(repo));
    std::string base(40, 'a'), target(40, 'b');
    ASSERT_TRUE(navigation::complete_comparison(repo, request, base, target));
    ASSERT_EQ(repo.workspace().history().size(), count);
    ASSERT_EQ(repo.comparisonScope(), "compare:" + base + ":" + target);
    ASSERT_EQ(repo.selectedFilePath(), "changed.cpp");
    open_kept(repo, reading::review("wt"));
    ASSERT_FALSE(navigation::complete_comparison(repo, request, base, target));
    ASSERT_FALSE(repo.comparisonOpen());
}

TEST(opening_source_keeps_the_retained_comparisons_review_storage) {
    ecs::RepoComponent repo;
    ecs::ReviewComponent review;
    repo.repoPath = "repo";
    open_kept(repo, reading::review("compare:base:target"));
    auto scope = ecs::selected_review_storage_scope(repo, review);
    open_kept(repo, reading::source("changed.cpp", "target"));
    ASSERT_FALSE(repo.comparisonOpen());
    ASSERT_EQ(ecs::selected_review_storage_scope(repo, review), scope);
    navigation::return_to_review(repo);
    ASSERT_TRUE(repo.comparisonOpen());
}

TEST(consuming_a_source_reveal_does_not_restore_a_stale_line_on_activation) {
    ecs::RepoComponent repo;
    open_kept(repo, reading::source("file.cpp", "", 42));
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
    open_kept(repo, reading::review("compare:main:topic"));
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
    open_kept(repo, reading::review("compare:main:topic"));
    auto request = navigation::stamp(repo, navigation::comparison_request_key(repo));
    repo.comparisonBase = "unsubmitted";
    repo.comparisonTarget = "edits";
    repo.comparisonMergeBase = true;
    ASSERT_TRUE(navigation::complete_comparison(repo, request, std::string(40, 'a'), std::string(40, 'b')));
}

TEST(document_collection_retains_two_reviews_and_three_sources) {
    ecs::RepoComponent repo;
    open_kept(repo, reading::review("first", "one.cpp"));
    auto first = repo.workspace().active_id();
    open_kept(repo, reading::review("second", "two.cpp"));
    auto second = repo.workspace().active_id();
    std::vector<reading::DocumentId> sources;
    for (const auto& path : {"a.cpp", "b.cpp", "c.cpp"}) {
        open_kept(repo, reading::source(path, "second", 12));
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
    open_kept(repo, reading::review("commit", "a.cpp"));
    auto review = repo.workspace().active_id();
    open_kept(repo, reading::review("commit", "b.cpp"));
    ASSERT_EQ(repo.workspace().active_id(), review);
    open_kept(repo, reading::source("a.cpp", "commit", 12));
    auto source = repo.workspace().active_id();
    open_kept(repo, reading::source("a.cpp", "commit", 99, reading::review("another")));
    ASSERT_EQ(repo.workspace().active_id(), source);
    ASSERT_EQ(repo.fullFileTargetLine(), 99);
    ASSERT_EQ(repo.workspace().documents().size(), 3u);
    open_kept(repo, reading::source("a.cpp", "INDEX"));
    ASSERT_FALSE(repo.workspace().active_id() == source);
}

TEST(inactive_documents_release_rendering_payloads_and_keep_file_summaries) {
    ecs::RepoComponent repo;
    ecs::CommitDetailCache cache;
    open_kept(repo, reading::review("commit"));
    cache.cachedCommitHash = "commit";
    cache.commitDetailBody = std::string(10000, 'x');
    ecs::FileDiff file;
    file.filePath = "file.cpp";
    file.additions = 7;
    file.hunks.push_back({});
    file.hunks.back().lines.push_back(std::string(10000, 'y'));
    cache.commitDetailDiff.push_back(file);
    navigation::remember_review_files(repo, cache.commitDetailDiff);
    open_kept(repo, reading::source("file.cpp", "commit"));
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
    open_kept(repo, reading::review("compare:base:target"));
    auto comparison = repo.workspace().active_id();
    repo.comparisonLoadedScope = repo.comparisonScope();
    repo.comparisonNeedsLoad = false;
    repo.comparisonDiff.resize(100);
    open_kept(repo, reading::review("another"));
    ASSERT_EQ(repo.comparisonDiff.capacity(), 0u);
    ASSERT_TRUE(repo.comparisonLoadedScope.empty());
    navigation::activate(repo, comparison);
    ASSERT_TRUE(repo.comparisonNeedsLoad);
}

TEST(resolved_aliases_reuse_existing_source_review_and_comparison_documents) {
    const std::string oid(40, 'a'), parent(40, 'b');
    for (int kind = 0; kind < 3; ++kind) {
        ecs::RepoComponent repo;
        if (kind == 0) open_kept(repo, reading::source("file.cpp", oid));
        if (kind == 1) open_kept(repo, reading::review(oid));
        if (kind == 2) open_kept(repo, reading::review("compare:" + parent + ":" + oid));
        auto existing = repo.workspace().active_id();
        if (kind == 0) open_kept(repo, reading::source("file.cpp", "main", 17));
        if (kind == 1) open_kept(repo, reading::review("main", "file.cpp"));
        if (kind == 2) open_kept(repo, reading::review("compare:base:main", "file.cpp"));
        ASSERT_EQ(repo.workspace().documents().size(), 3u);
        auto request = navigation::stamp(repo, kind == 2 ? navigation::comparison_request_key(repo) : "request");
        if (kind == 0) ASSERT_TRUE(navigation::resolve_source(repo, request, oid));
        if (kind == 1) ASSERT_TRUE(navigation::resolve_review(repo, request, oid, parent));
        if (kind == 2) ASSERT_TRUE(navigation::complete_comparison(repo, request, parent, oid));
        ASSERT_EQ(repo.workspace().active_id(), existing);
        ASSERT_EQ(repo.workspace().documents().size(), 2u);
        open_kept(repo, reading::review("wt"));
        navigation::activate(repo, existing);
        ASSERT_EQ(repo.workspace().active_id(), existing);
        if (kind == 0) ASSERT_EQ(repo.fullFileTargetLine(), 17);
        else ASSERT_EQ(repo.selectedFilePath(), "file.cpp");
    }
}

TEST(inactive_review_summaries_preserve_filters_rename_comments_and_progress) {
    ecs::RepoComponent repo;
    ecs::ReviewComponent review;
    open_kept(repo, reading::review("commit"));
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
    open_kept(repo, reading::source("M.cpp", "commit"));
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
        open_kept(repo, reading::review(comparison ? "compare:base:main" : "main", "original.cpp"));
        auto review = repo.workspace().active_id();
        open_kept(repo, reading::source("file.cpp", oid));
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
    open_kept(repo, reading::review("commit", "original.cpp"));
    auto review = repo.workspace().active_id();
    ecs::FileDiff file;
    file.filePath = "original.cpp";
    file.hunks.push_back({});
    file.hunks[0].lines = {"+line"};
    navigation::remember_review_files(repo, {file});
    open_kept(repo, reading::source("original.cpp", "commit", 42));
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
        open_kept(repo, reading::source(std::to_string(i), ""));
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
    open_kept(repo, reading::review("main"));
    auto review = repo.workspace().active_id();
    open_kept(repo, reading::source("source.cpp", oid));
    navigation::close(repo, repo.workspace().active_id());
    navigation::activate(repo, review);
    auto stamp = navigation::stamp(repo, "request");
    ASSERT_TRUE(navigation::resolve_review(repo, stamp, oid, ""));
    navigation::reopen_closed(repo);
    ASSERT_EQ(repo.workspace().source()->origin->destination, reading::review(oid).destination);
}

TEST(previews_replace_in_place_without_closing_kept_documents) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review("a"));
    auto a = repo.workspace().active_id();
    navigation::open(repo, reading::review("b"));
    auto b = repo.workspace().active_id();
    ASSERT_TRUE(repo.workspace().document(a) == nullptr);
    ASSERT_EQ(repo.workspace().documents().size(), 2u);
    ASSERT_EQ(repo.workspace().documents()[1].id, b);
    ASSERT_TRUE(repo.workspace().closed().empty());
    navigation::keep(repo, b);
    navigation::open(repo, reading::review("c"));
    navigation::open(repo, reading::review("d"));
    ASSERT_EQ(repo.workspace().documents().size(), 3u);
    ASSERT_FALSE(repo.workspace().document(b)->preview);
    navigation::activate(repo, b);
    ASSERT_FALSE(repo.workspace().document(b)->preview);
}

TEST(source_previews_keep_their_origin_and_replace_only_other_previews) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review("commit", "a.cpp"));
    auto origin = repo.workspace().active_id();
    navigation::open(repo, reading::source("a.cpp", "commit"));
    auto source = repo.workspace().active_id();
    ASSERT_FALSE(repo.workspace().document(origin)->preview);
    navigation::open(repo, reading::source("b.cpp", "commit"));
    ASSERT_TRUE(repo.workspace().document(source) == nullptr);
    ASSERT_TRUE(repo.workspace().document(origin) != nullptr);
    ASSERT_EQ(repo.workspace().documents().size(), 3u);
    navigation::return_to_review(repo);
    ASSERT_EQ(repo.workspace().active_id(), origin);
    ASSERT_EQ(repo.selectedFilePath(), "a.cpp");
}

TEST(keeping_a_preview_does_not_restart_its_pending_read) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::source("same.cpp"));
    std::promise<ecs::FullFileContent> promise;
    std::stop_source stop;
    repo.fullFileFuture = {promise.get_future(), stop};
    auto stamp = navigation::stamp(repo, "page");
    auto id = repo.workspace().active_id();
    navigation::keep(repo, id);
    ASSERT_FALSE(repo.workspace().document(id)->preview);
    ASSERT_TRUE(navigation::accepts(repo, stamp, "page"));
    ASSERT_TRUE(repo.fullFileFuture.valid());
    ASSERT_FALSE(stop.stop_requested());
}

TEST(double_click_and_enter_keep_only_the_requested_destination) {
    ecs::RepoComponent repo;
    auto now = std::chrono::steady_clock::now();
    navigation::click(repo, reading::review("a"), false, reading::ClickRegion::Tree, now);
    navigation::click(repo, reading::review("a"), false, reading::ClickRegion::Tree, now + std::chrono::milliseconds(300));
    ASSERT_FALSE(repo.workspace().document(repo.workspace().active_id())->preview);
    navigation::click(repo, reading::review("b"), false, reading::ClickRegion::Tree, now + std::chrono::milliseconds(400));
    ASSERT_TRUE(repo.workspace().document(repo.workspace().active_id())->preview);
    navigation::click(repo, reading::review("b"), false, reading::ClickRegion::Tree, now + std::chrono::milliseconds(1000));
    ASSERT_TRUE(repo.workspace().document(repo.workspace().active_id())->preview);
    navigation::click(repo, reading::review("b"), true, reading::ClickRegion::Tree, now + std::chrono::milliseconds(2000));
    ASSERT_FALSE(repo.workspace().document(repo.workspace().active_id())->preview);
}

TEST(resolving_a_preview_alias_preserves_the_existing_kept_tab) {
    ecs::RepoComponent repo;
    std::string oid(40, 'a');
    open_kept(repo, reading::review(oid));
    auto kept = repo.workspace().active_id();
    navigation::open(repo, reading::review("HEAD"));
    auto stamp = navigation::stamp(repo, "patch");
    ASSERT_TRUE(navigation::resolve_review(repo, stamp, oid, ""));
    ASSERT_EQ(repo.workspace().active_id(), kept);
    ASSERT_FALSE(repo.workspace().document(kept)->preview);
    ASSERT_EQ(repo.workspace().documents().size(), 2u);
}

TEST(reopen_keeps_a_closed_preview_without_replacing_the_current_preview) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::source("a.cpp"));
    auto a = repo.workspace().active_id();
    navigation::close(repo, a);
    navigation::open(repo, reading::source("b.cpp"));
    auto b = repo.workspace().active_id();
    navigation::reopen_closed(repo);
    ASSERT_EQ(repo.workspace().active_id(), a);
    ASSERT_FALSE(repo.workspace().document(a)->preview);
    ASSERT_TRUE(repo.workspace().document(b)->preview);
}

TEST(back_recreates_a_replaced_preview_and_invalidates_its_old_request) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review("a"));
    auto stamp = navigation::stamp(repo, "patch");
    navigation::open(repo, reading::review("b"));
    navigation::step(repo, -1);
    ASSERT_EQ(repo.selectedCommitHash(), "a");
    ASSERT_EQ(repo.workspace().documents().size(), 2u);
    ASSERT_FALSE(navigation::accepts(repo, stamp, "patch"));
}

TEST(clicks_in_different_regions_do_not_count_as_a_double_click) {
    ecs::RepoComponent repo;
    auto now = std::chrono::steady_clock::now();
    navigation::click(repo, reading::source("a.cpp"), false, reading::ClickRegion::Picker, now);
    navigation::click(repo, reading::source("a.cpp"), false, reading::ClickRegion::Tabs, now + std::chrono::milliseconds(10));
    ASSERT_TRUE(repo.workspace().document(repo.workspace().active_id())->preview);
    navigation::click(repo, reading::source("a.cpp"), false, reading::ClickRegion::Tabs, now + std::chrono::milliseconds(20));
    ASSERT_FALSE(repo.workspace().document(repo.workspace().active_id())->preview);
}

TEST(history_navigation_breaks_a_double_click_sequence) {
    ecs::RepoComponent repo;
    auto now = std::chrono::steady_clock::now();
    navigation::click(repo, reading::review("a"), false, reading::ClickRegion::History, now);
    navigation::step(repo, -1);
    navigation::click(repo, reading::review("a"), false, reading::ClickRegion::History, now + std::chrono::milliseconds(10));
    ASSERT_TRUE(repo.workspace().document(repo.workspace().active_id())->preview);
}

TEST(different_rows_in_one_review_do_not_count_as_a_double_click) {
    ecs::RepoComponent repo;
    auto now = std::chrono::steady_clock::now();
    navigation::click(repo, reading::review("commit", "a.cpp"), false, reading::ClickRegion::Tree, now);
    navigation::click(repo, reading::review("commit", "b.cpp"), false, reading::ClickRegion::Tree, now + std::chrono::milliseconds(10));
    ASSERT_TRUE(repo.workspace().document(repo.workspace().active_id())->preview);
}

TEST(commit_subject_is_seeded_and_survives_switching_closing_and_alias_resolution) {
    ecs::RepoComponent repo;
    const std::string oid(40, 'a');
    ecs::CommitEntry entry;
    entry.hash = oid;
    entry.subject = "Recognizable subject";
    repo.commitLog.push_back(entry);
    open_kept(repo, reading::review(oid));
    const auto id = repo.workspace().active_id();
    ASSERT_STREQ(repo.workspace().document(id)->subject, entry.subject);
    navigation::open(repo, reading::review("HEAD"));
    navigation::remember_commit_subject(repo, entry.subject);
    auto stamp = navigation::stamp(repo, "patch");
    ASSERT_TRUE(navigation::resolve_review(repo, stamp, oid, ""));
    ASSERT_EQ(repo.workspace().active_id(), id);
    ASSERT_STREQ(repo.workspace().document(id)->subject, entry.subject);
    navigation::close(repo, id);
    navigation::reopen_closed(repo);
    ASSERT_STREQ(repo.workspace().document(id)->subject, entry.subject);
    navigation::open(repo, reading::source("a.cpp"));
    navigation::remember_commit_subject(repo, "Wrong document");
    ASSERT_TRUE(repo.workspace().document(repo.workspace().active_id())->subject.empty());
}

TEST(closing_into_working_changes_keeps_the_review_visible) {
    ecs::RepoComponent repo;
    open_kept(repo, reading::source("a.cpp"), false);
    navigation::close(repo, repo.workspace().active_id());
    ASSERT_EQ(repo.workspace().documents().size(), 1u);
    ASSERT_EQ(repo.workspace().location(), reading::Location{reading::ReviewLocation{}});
    ASSERT_TRUE(repo.workspace().history()[repo.workspace().history_index()].reviewing);
    ASSERT_TRUE(repo.navigationEffect->reviewing.value_or(false));
    open_kept(repo, reading::review("commit"), false);
    const auto review = repo.workspace().active_id();
    navigation::close(repo, repo.workspace().documents().front().id);
    navigation::close(repo, review);
    ASSERT_EQ(repo.workspace().documents().size(), 1u);
    ASSERT_TRUE(repo.workspace().history()[repo.workspace().history_index()].reviewing);
}

TEST(closing_inactive_documents_preserves_the_active_read_and_selects_nearest_neighbors) {
    ecs::RepoComponent repo;
    open_kept(repo, reading::source("a.cpp"));
    auto a = repo.workspace().active_id();
    open_kept(repo, reading::source("b.cpp"));
    auto b = repo.workspace().active_id();
    open_kept(repo, reading::source("c.cpp"));
    auto c = repo.workspace().active_id();
    repo.fullFileCacheKey = "c.cpp loaded";
    auto stamp = navigation::stamp(repo, "c.cpp read");
    navigation::close(repo, b);
    ASSERT_EQ(repo.workspace().active_id(), c);
    ASSERT_STREQ(repo.fullFileCacheKey, "c.cpp loaded");
    ASSERT_TRUE(navigation::accepts(repo, stamp, "c.cpp read"));
    navigation::activate(repo, a);
    navigation::close(repo, a);
    ASSERT_EQ(repo.workspace().active_id(), c);
    navigation::close(repo, c);
    ASSERT_EQ(repo.workspace().active_id(), repo.workspace().documents().front().id);
}

TEST(reopening_a_recreated_review_restores_its_retained_subject) {
    ecs::RepoComponent repo;
    open_kept(repo, reading::review("commit"));
    navigation::remember_commit_subject(repo, "Retained subject");
    navigation::close(repo, repo.workspace().active_id());
    navigation::open(repo, reading::review("commit"));
    auto recreated = repo.workspace().active_id();
    ASSERT_TRUE(repo.workspace().document(recreated)->subject.empty());
    navigation::reopen_closed(repo);
    ASSERT_EQ(repo.workspace().active_id(), recreated);
    ASSERT_STREQ(repo.workspace().document(recreated)->subject, "Retained subject");
}

TEST(reordering_changes_only_order_and_preserves_requests_and_history) {
    ecs::RepoComponent repo;
    open_kept(repo, reading::source("a.cpp"));
    const auto a = repo.workspace().active_id();
    open_kept(repo, reading::source("b.cpp"));
    const auto b = repo.workspace().active_id();
    navigation::open(repo, reading::source("c.cpp"));
    const auto c = repo.workspace().active_id();
    const auto generation = repo.workspace().generation();
    const auto visits = repo.workspace().history();
    repo.fullFileCacheKey = "retained";
    const auto request = navigation::stamp(repo, "reader");
    ASSERT_TRUE(navigation::reorder(repo, c, 0));
    ASSERT_EQ(repo.workspace().documents()[0].id.value, c.value);
    ASSERT_EQ(repo.workspace().active_id().value, c.value);
    ASSERT_TRUE(repo.workspace().document(c)->preview);
    ASSERT_EQ(repo.workspace().generation(), generation);
    ASSERT_TRUE(repo.workspace().history() == visits);
    ASSERT_EQ(repo.fullFileCacheKey, std::string("retained"));
    ASSERT_TRUE(navigation::accepts(repo, request, "reader"));
    ASSERT_TRUE(navigation::reorder(repo, c, 4));
    ASSERT_EQ(repo.workspace().documents()[3].id.value, c.value);
    ASSERT_TRUE(navigation::reorder(repo, b, 1));
    ASSERT_EQ(repo.workspace().documents()[1].id.value, b.value);
    ASSERT_FALSE(navigation::reorder(repo, a, 2));
    ASSERT_FALSE(navigation::reorder(repo, a, 3));
    ASSERT_FALSE(navigation::reorder(repo, a, 99));
    ASSERT_FALSE(navigation::reorder(repo, reading::DocumentId{99}, 0));
    ASSERT_EQ(repo.workspace().generation(), generation);
}

TEST(recent_documents_follow_activation_and_ignore_tab_order_and_closed_tabs) {
    ecs::RepoComponent repo;
    open_kept(repo, reading::source("a.cpp"));
    const auto a = repo.workspace().active_id();
    open_kept(repo, reading::source("b.cpp"));
    const auto b = repo.workspace().active_id();
    open_kept(repo, reading::source("c.cpp"));
    const auto c = repo.workspace().active_id();
    navigation::activate(repo, a);
    auto recent = reading::recent_documents(repo.workspace());
    ASSERT_EQ(recent[0], a);
    ASSERT_EQ(recent[1], c);
    ASSERT_EQ(recent[2], b);
    navigation::reorder(repo, b, 0);
    ASSERT_TRUE(reading::recent_documents(repo.workspace()) == recent);
    navigation::close(repo, c);
    recent = reading::recent_documents(repo.workspace());
    ASSERT_EQ(recent.size(), size_t{3});
    ASSERT_EQ(recent[0], a);
    ASSERT_EQ(recent[1], b);
    navigation::reopen_closed(repo);
    ASSERT_EQ(reading::recent_documents(repo.workspace())[0], c);
}

TEST(saved_sessions_exclude_previews_and_restore_kept_tabs_without_payloads) {
    ecs::RepoComponent repo;
    open_kept(repo, reading::source("a.cpp", std::string(40, 'a')));
    navigation::remember_anchor(repo, {"a.cpp", std::string(40, 'a'), reading::DiffSide::After, 120, 5, .2f, ' '});
    const auto a = repo.workspace().active_id();
    open_kept(repo, reading::review(std::string(40, 'b')));
    navigation::remember_commit_subject(repo, "Kept review");
    const auto commit = repo.workspace().active_id();
    navigation::reorder(repo, a, 0);
    navigation::open(repo, reading::source("preview.cpp"));
    const auto saved = reading::save_session(repo.workspace());
    ASSERT_EQ(saved.documents.size(), size_t{3});
    ASSERT_TRUE(reading::same_document(saved.documents[0].location, reading::source("a.cpp", std::string(40, 'a'))));
    ASSERT_TRUE(reading::same_document(saved.documents[saved.active].location, repo.workspace().document(commit)->location));
    const auto before = navigation::stamp(repo, "pending");
    navigation::restore_session(repo, saved, false);
    ASSERT_EQ(repo.workspace().documents().size(), size_t{3});
    ASSERT_FALSE(navigation::accepts(repo, before, "pending"));
    for (const auto& document : repo.workspace().documents()) {
        ASSERT_FALSE(document.preview);
        ASSERT_FALSE(document.files.has_value());
    }
    ASSERT_TRUE(repo.fullFileDiff.empty());
    ASSERT_TRUE(repo.workspace().documents()[0].restoreAnchor);
    navigation::activate(repo, repo.workspace().documents()[0].id);
    ASSERT_EQ(repo.fullFileTargetLine(), 120);
    navigation::restored_anchor(repo);
    ASSERT_EQ(repo.fullFileTargetLine(), 0);
    ASSERT_FALSE(repo.workspace().documents()[0].restoreAnchor);
}

TEST(unresolved_saved_revisions_require_explicit_resolution) {
    ecs::RepoComponent repo;
    reading::ReadingSession session{{{reading::source("a.cpp", "HEAD")}, {reading::review("HEAD")}}, 0};
    navigation::restore_session(repo, session, false);
    ASSERT_TRUE(repo.workspace().documents()[0].unresolvedSavedRevision);
    ASSERT_TRUE(repo.workspace().documents()[1].unresolvedSavedRevision);
    const auto generation = repo.workspace().generation();
    navigation::activate(repo, repo.workspace().active_id());
    ASSERT_TRUE(repo.workspace().documents()[0].unresolvedSavedRevision);
    navigation::resolve_saved_revision(repo);
    ASSERT_FALSE(repo.workspace().documents()[0].unresolvedSavedRevision);
    ASSERT_TRUE(repo.workspace().generation() > generation);
    ASSERT_TRUE(repo.fullFileFuture.valid() == false);
    ASSERT_TRUE(repo.workspace().documents()[1].unresolvedSavedRevision);
}

TEST(restored_active_source_wins_over_saved_recency) {
    ecs::RepoComponent repo;
    reading::ReadingSession session{{{reading::source("recent.cpp"), "", 99},
        {reading::source("missing.cpp", std::string(40, 'f')), "", 1}}, 1};
    navigation::restore_session(repo, session, false);
    ASSERT_EQ(repo.fullFilePath(), std::string("missing.cpp"));
    ASSERT_EQ(repo.fullFileRevision(), std::string(40, 'f'));
    ASSERT_EQ(repo.workspace().source()->destination.path, std::string("missing.cpp"));
}

TEST(restoring_tabs_preserves_the_window_review_mode) {
    ecs::RepoComponent repo;
    reading::ReadingSession session{{{reading::review("wt")}}, 0};
    navigation::restore_session(repo, session, false);
    ASSERT_FALSE(*repo.navigationEffect->reviewing);
    ASSERT_FALSE(repo.workspace().history().front().reviewing);
    navigation::restore_session(repo, session, true);
    ASSERT_TRUE(*repo.navigationEffect->reviewing);
    ASSERT_TRUE(repo.workspace().history().front().reviewing);
}

TEST(history_keeps_distinct_file_anchors_in_one_commit_without_scroll_visits) {
    ecs::RepoComponent repo;
    const std::string oid(40, 'a');
    open_kept(repo, reading::review(oid, "a.cpp"));
    const auto id = repo.workspace().active_id();
    const auto count = repo.workspace().history().size();
    const reading::ReadingAnchor a{"a.cpp", oid, reading::DiffSide::Before, 81, 7, .3f, '-'};
    navigation::remember_anchor(repo, a);
    navigation::remember_anchor(repo, {"a.cpp", oid, reading::DiffSide::Before, 90, 9, .2f, '-'});
    navigation::remember_anchor(repo, a);
    ASSERT_EQ(repo.workspace().history().size(), count);
    navigation::activate(repo, id);
    ASSERT_EQ(repo.workspace().history().size(), count);
    open_kept(repo, reading::review(oid, "b.cpp"));
    const reading::ReadingAnchor b{"b.cpp", oid, reading::DiffSide::After, 31, 4, .4f, '+'};
    navigation::remember_anchor(repo, b);
    ASSERT_EQ(repo.workspace().active_id(), id);
    navigation::step(repo, -1);
    ASSERT_EQ(repo.selectedFilePath(), std::string("a.cpp"));
    ASSERT_TRUE(repo.workspace().document(id)->restoreAnchor);
    ASSERT_TRUE(repo.workspace().document(id)->anchor == a);
    navigation::remember_anchor(repo, b);
    ASSERT_TRUE(repo.workspace().history()[repo.workspace().history_index()].anchor == a);
    navigation::restored_anchor(repo);
    navigation::step(repo, 1);
    ASSERT_EQ(repo.selectedFilePath(), std::string("b.cpp"));
    ASSERT_TRUE(repo.workspace().document(id)->anchor == b);
}

TEST(history_reopens_closed_sources_with_the_visit_origin_and_requested_line) {
    ecs::RepoComponent repo;
    const auto origin = reading::review(std::string(40, 'a'), "renamed.cpp");
    open_kept(repo, origin);
    open_kept(repo, reading::source("old.cpp", std::string(40, 'b'), 300, origin));
    const auto source = repo.workspace().active_id();
    const reading::ReadingAnchor anchor{"old.cpp", std::string(40, 'b'), reading::DiffSide::After, 298, 8, .25f, ' '};
    navigation::remember_anchor(repo, anchor);
    navigation::clear_source_reveal(repo);
    ASSERT_EQ(std::get<reading::SourceLocation>(repo.workspace().history().back().location).line, 300);
    navigation::close(repo, source);
    ASSERT_TRUE(repo.workspace().document(source) == nullptr);
    navigation::step(repo, -1);
    ASSERT_EQ(repo.fullFilePath(), std::string("old.cpp"));
    ASSERT_EQ(repo.fullFileTargetLine(), 298);
    ASSERT_TRUE(repo.workspace().source()->origin == origin);
    ASSERT_TRUE(repo.workspace().document(repo.workspace().active_id())->anchor == anchor);
}

TEST(history_truncates_forward_visits_and_retains_only_256_precise_locations) {
    ecs::RepoComponent repo;
    for (int i = 1; i <= 300; ++i) {
        open_kept(repo, reading::source("a.cpp", "", i));
        navigation::remember_anchor(repo, {"a.cpp", "", reading::DiffSide::After, i, i + 1, .1f, ' '});
    }
    ASSERT_EQ(repo.workspace().history().size(), size_t{256});
    ASSERT_EQ(repo.workspace().history().front().anchor->line, 45);
    navigation::step(repo, -1);
    navigation::step(repo, -1);
    const auto count = repo.workspace().history_index() + 2;
    open_kept(repo, reading::source("new.cpp"));
    ASSERT_EQ(repo.workspace().history().size(), count);
    const auto generation = repo.workspace().generation();
    navigation::step(repo, 1);
    ASSERT_EQ(repo.workspace().generation(), generation);
    ASSERT_EQ(repo.fullFilePath(), std::string("new.cpp"));
}

TEST(opening_a_deleted_diff_line_uses_the_before_revision_and_old_path) {
    ecs::RepoComponent repo;
    const std::string before(40, 'a'), after(40, 'b');
    open_kept(repo, reading::review("compare:" + before + ":" + after, "renamed.cpp"));
    const reading::ReadingAnchor origin{"renamed.cpp", "compare:" + before + ":" + after, reading::DiffSide::Before, 40, 7, .3f, '-'};
    navigation::remember_anchor(repo, origin);
    ecs::FileDiff file;
    file.filePath = "renamed.cpp";
    file.oldPath = "old.cpp";
    navigation::open_source(repo, file, origin);
    ASSERT_EQ(repo.fullFilePath(), std::string("old.cpp"));
    ASSERT_EQ(repo.fullFileRevision(), before);
    ASSERT_EQ(repo.fullFileTargetLine(), 40);
    ASSERT_EQ(repo.workspace().source()->column, 7);
    ASSERT_TRUE(repo.workspace().source()->originAnchor == origin);
    ASSERT_FALSE(repo.workspace().documents()[1].preview);
    navigation::return_to_review(repo);
    ASSERT_EQ(repo.selectedFilePath(), std::string("renamed.cpp"));
    ASSERT_TRUE(repo.workspace().document(repo.workspace().active_id())->anchor == origin);
    ASSERT_TRUE(repo.workspace().document(repo.workspace().active_id())->restoreAnchor);
}

TEST(source_destinations_use_the_first_change_without_a_visible_or_selected_line) {
    ecs::FileDiff file;
    file.filePath = "new.cpp";
    file.oldPath = "old.cpp";
    ecs::DiffHunk hunk;
    hunk.oldStart = 10;
    hunk.newStart = 20;
    hunk.lines = {" context", "-removed", "+added"};
    file.hunks.push_back(hunk);
    auto source = reading::source_at_diff(reading::review("wt"), file);
    ASSERT_EQ(source.destination.path, std::string("old.cpp"));
    ASSERT_EQ(reading::revision_text(source.destination.revision), std::string("INDEX"));
    ASSERT_EQ(source.line, 11);
    ASSERT_EQ(reading::diff_text_at(file, 11, reading::DiffSide::Before), std::string_view("removed"));
    ASSERT_EQ(reading::diff_text_at(file, 21, reading::DiffSide::After), std::string_view("added"));
    auto added = reading::source_at_diff(reading::review("index"), file,
        reading::ReadingAnchor{"new.cpp", "index", reading::DiffSide::After, 21, 3, 0.f, '+'});
    ASSERT_EQ(added.destination.path, std::string("new.cpp"));
    ASSERT_EQ(reading::revision_text(added.destination.revision), std::string("INDEX"));
    ASSERT_EQ(added.line, 21);
    ASSERT_EQ(added.column, 3);
}

TEST(source_origin_position_survives_other_files_and_later_review_visits) {
    ecs::RepoComponent repo;
    const std::string oid(40, 'a');
    open_kept(repo, reading::review(oid, "a.cpp"));
    const reading::ReadingAnchor original{"a.cpp", oid, reading::DiffSide::After, 50, 8, .2f, '+'};
    navigation::remember_anchor(repo, original);
    open_kept(repo, reading::source("a.cpp", oid));
    const auto source = repo.workspace().active_id();
    open_kept(repo, reading::source("other.cpp"));
    ASSERT_TRUE(repo.workspace().source()->originAnchor == original);
    open_kept(repo, reading::review(oid, "b.cpp"));
    navigation::remember_anchor(repo, {"b.cpp", oid, reading::DiffSide::After, 100, 1, .5f, '+'});
    navigation::activate(repo, source);
    navigation::return_to_review(repo);
    ASSERT_EQ(repo.selectedFilePath(), std::string("a.cpp"));
    ASSERT_TRUE(repo.workspace().document(repo.workspace().active_id())->anchor == original);
}

TEST(source_column_navigation_survives_history_and_is_consumed_with_the_line) {
    ecs::RepoComponent repo;
    auto source = reading::source("a.cpp", "", 42);
    source.column = 20;
    open_kept(repo, source);
    navigation::clear_source_reveal(repo);
    ASSERT_EQ(repo.workspace().source()->column, 1);
    ASSERT_EQ(std::get<reading::SourceLocation>(repo.workspace().history().back().location).column, 20);
    navigation::activate(repo, repo.workspace().active_id());
    ASSERT_EQ(repo.workspace().history().size(), size_t{2});
    open_kept(repo, source);
    source.column = 30;
    open_kept(repo, source);
    ASSERT_TRUE(repo.fullFileNavigateFrames > 0);
    navigation::step(repo, -1);
    ASSERT_EQ(repo.workspace().source()->column, 20);
}

int main() { RUN_ALL_TESTS(); }
