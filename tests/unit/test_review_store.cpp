// Unit tests for review_store: durable per-repo review persistence.
// Round-trips a populated ReviewComponent through save_review/load_review and
// checks a missing file is a clean no-op.

#include "test_framework.h"
#include "../../src/util/navigation.h"

#include <afterhours/src/plugins/files.h>

#include "../../src/review_store.h"
#include "../../src/ecs/components.h"
#include "../../src/util/review_anchor.h"

#include <filesystem>
#include <future>

TEST(review_store_roundtrip) {
    ecs::ReviewComponent r;
    r.reviewing = true;
    r.basketOpen = false;
    r.comments.push_back({"wt", "src/foo.cpp", 42, "fix this"});
    r.comments.push_back({"abc123", "src/bar.h", 7, "nit"});
    r.comments[1].endLine = 9;
    r.comments[1].oldSide = true;
    r.comments[1].resolved = true;
    r.approvedHunks.insert("src/foo.cpp\n@@ -1 +1 @@");
    r.reviewedFiles["wt\nmode-only.sh"] = "metadata-signature";
    r.foldedHunks.insert("src/bar.h\n@@ -2 +2 @@");
    r.seenSig["src/foo.cpp"] = "1,2,3";
    r.baselineHead = "deadbeef";
    r.baselineDiffSig = "sig;";
    r.baselineSnapshot = "/local/review.baseline.cbor";

    const std::string repo = "/tmp/test_review_store_repo";
    review_store::save_review(repo, r);

    ecs::ReviewComponent r2;
    review_store::load_review(repo, r2);

    ASSERT_TRUE(r2.reviewing);
    ASSERT_TRUE(!r2.basketOpen);
    ASSERT_EQ((int)r2.comments.size(), 2);
    ASSERT_STREQ(r2.comments[0].file, "src/foo.cpp");
    ASSERT_EQ(r2.comments[0].line, 42);
    ASSERT_STREQ(r2.comments[1].scope, "abc123");
    ASSERT_EQ(r2.comments[1].endLine, 9);
    ASSERT_TRUE(r2.comments[1].oldSide);
    ASSERT_TRUE(r2.comments[1].resolved);
    ASSERT_EQ(ecs::comment_location(r2.comments[1]), "src/bar.h:7-9 (old)");
    ASSERT_TRUE(r2.approvedHunks.count("src/foo.cpp\n@@ -1 +1 @@") == 1);
    ASSERT_EQ(r2.reviewedFiles, r.reviewedFiles);
    ASSERT_TRUE(r2.foldedHunks.count("src/bar.h\n@@ -2 +2 @@") == 1);
    ASSERT_STREQ(r2.seenSig["src/foo.cpp"], "1,2,3");
    ASSERT_STREQ(r2.baselineHead, "deadbeef");
    ASSERT_EQ(r2.baselineSnapshot, r.baselineSnapshot);

    std::filesystem::remove(review_store::review_path(repo));
}

TEST(review_store_missing_is_noop) {
    ecs::ReviewComponent r;
    review_store::load_review("/tmp/test_review_store_nonexistent_xyz", r);
    ASSERT_TRUE(!r.reviewing);
    ASSERT_TRUE(r.comments.empty());
}

TEST(comparison_comments_keep_both_revisions_and_restore_the_comparison) {
    ecs::DiffHunk hunk;
    hunk.oldStart = hunk.newStart = 1;
    hunk.header = "@@ -1 +1 @@";
    hunk.lines = {"-before", "+after"};
    auto comment = ecs::comment_with_context({"compare:base:target", "file.cpp", 1, "question", 1, true}, hunk, "head");
    ASSERT_TRUE(comment.revision.starts_with("base"));
    ASSERT_TRUE(comment.revision.find("compare:base:target^") == std::string::npos);
    ecs::ReviewComponent review;
    ecs::begin_comment(review, "comparison-hunk", comment);
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review(comment.scope));
    navigation::restore_draft(repo, review);
    ASSERT_TRUE(repo.comparisonOpen());
    ASSERT_TRUE(repo.selectedCommitHash().empty());
    review.comments.push_back(comment);
    ASSERT_TRUE(ecs::build_review_markdown(review, "branch").find("comparison base → target") != std::string::npos);
}

TEST(editing_a_comment_preserves_its_location) {
    ecs::ReviewComponent review;
    review.comments.push_back({"abc", "file.cpp", 12, "before", 14, true});
    review.editingComment = 0;
    review.editingCommentText = "after\nsecond line";
    ASSERT_TRUE(ecs::save_comment_edit(review));
    ASSERT_EQ(review.comments.size(), 1u);
    ASSERT_EQ(review.comments.front().text, "after\nsecond line");
    ASSERT_EQ(ecs::comment_location(review.comments.front()), "file.cpp:12-14 (old)");
    ASSERT_TRUE(review.dirty);
    ASSERT_EQ(review.editingComment, -1);
    ASSERT_FALSE(ecs::save_comment_edit(review));
}

TEST(resolved_comments_stay_saved_but_leave_the_feedback_export) {
    ecs::ReviewComponent review;
    review.comments.push_back({"wt", "code.cpp", 1, "open note"});
    review.comments.push_back({"abc", "code.cpp", 2, "closed note", 2, false, true});
    ASSERT_EQ(ecs::unresolved_comment_count(review), 1u);
    auto markdown = ecs::build_review_markdown(review, "main");
    ASSERT_TRUE(markdown.find("open note") != std::string::npos);
    ASSERT_TRUE(markdown.find("closed note") == std::string::npos);
    ASSERT_TRUE(markdown.find("commit abc") == std::string::npos);
    review.comments[1].resolved = false;
    ASSERT_TRUE(ecs::build_review_markdown(review, "main").find("closed note") != std::string::npos);
    for (auto& comment : review.comments) comment.resolved = true;
    ASSERT_TRUE(ecs::build_review_markdown(review, "main").empty());
}

TEST(unfinished_comments_and_edits_survive_a_roundtrip) {
    ecs::ReviewComponent review;
    ecs::begin_comment(review, "hunk-a", {"wt", "a.cpp", 3, "", 5, false});
    review.composingText = "Draft A";
    ecs::begin_comment(review, "hunk-b", {"abc", "b.cpp", 8, "", 9, true});
    review.composingText = "Draft B\nsecond line";
    review.comments.push_back({"wt", "c.cpp", 1, "queued text"});
    review.editingComment = 0;
    review.editingCommentText = "Unsaved edit";
    const std::string path = "/tmp/floatinghotel_draft_roundtrip";
    review_store::save_review(path, review);
    ecs::ReviewComponent loaded;
    review_store::load_review(path, loaded);
    ASSERT_EQ(loaded.composingText, "Draft B\nsecond line");
    ASSERT_EQ(loaded.composingLine, 8);
    ASSERT_TRUE(loaded.composingOldSide);
    ASSERT_EQ(loaded.drafts.at("hunk-a").text, "Draft A");
    ASSERT_EQ(loaded.editingCommentText, "Unsaved edit");
    ASSERT_EQ(loaded.comments.front().text, "queued text");
    ecs::begin_comment(loaded, "hunk-a", {});
    ASSERT_EQ(loaded.composingText, "Draft A");
    ecs::commit_pending_comment(loaded);
    ASSERT_FALSE(loaded.drafts.contains("hunk-a"));
    ASSERT_TRUE(loaded.drafts.contains("hunk-b"));
    ASSERT_EQ(loaded.comments.size(), 2u);
    ecs::RepoComponent repo;
    navigation::restore_draft(repo, loaded);
    ASSERT_EQ(repo.selectedFilePath(), "c.cpp");
    std::filesystem::remove(review_store::review_path(path));
}

TEST(deletion_is_persisted_and_failed_writes_keep_the_dirty_flag) {
    ecs::ReviewComponent review;
    review.comments.push_back({"wt", "code.cpp", 1, "remove me"});
    review.foldedHunks.insert("wt\ncode.cpp\nsignature");
    const std::string path = "/tmp/floatinghotel_delete_roundtrip";
    ASSERT_TRUE(review_store::save_review(path, review));
    ecs::erase_comment(review, 0);
    ASSERT_TRUE(review.dirty);
    ASSERT_TRUE(review.foldedHunks.empty());
    ASSERT_FALSE(review_store::persist_review("", review));
    ASSERT_TRUE(review.dirty);
    ASSERT_TRUE(review_store::persist_review(path, review));
    ASSERT_FALSE(review.dirty);
    ecs::ReviewComponent loaded;
    review_store::load_review(path, loaded);
    ASSERT_TRUE(loaded.comments.empty());
    std::filesystem::remove(review_store::review_path(path));
}

TEST(review_state_is_isolated_by_branch_and_revision) {
    const std::string repo = "/tmp/floatinghotel_scoped_review";
    ecs::ReviewComponent review;
    ASSERT_TRUE(review_store::switch_review_scope(repo, "main@one", review));
    review.comments.push_back({"wt", "code.cpp", 1, "main note"});
    review.approvedHunks.insert("main approval");
    review.dirty = true;
    ASSERT_TRUE(review_store::switch_review_scope(repo, "other@one", review));
    ASSERT_TRUE(review.comments.empty());
    ASSERT_TRUE(review.approvedHunks.empty());
    review.comments.push_back({"wt", "code.cpp", 1, "other note"});
    review.dirty = true;
    ASSERT_TRUE(review_store::switch_review_scope(repo, "main@one", review));
    ASSERT_EQ(review.comments.front().text, "main note");
    ASSERT_TRUE(review.approvedHunks.contains("main approval"));
    ASSERT_TRUE(review_store::switch_review_scope(repo, "main@two", review));
    ASSERT_TRUE(review.comments.empty());
    ASSERT_TRUE(review_store::switch_review_scope(repo, "other@one", review));
    ASSERT_EQ(review.comments.front().text, "other note");
    for (const auto& scope : {"main@one", "other@one", "main@two"})
        std::filesystem::remove(review_store::review_path(repo, scope));
}

TEST(review_signatures_detect_same_size_edits) {
    ecs::FileDiff before;
    before.filePath = "main.cpp";
    before.additions = 1;
    before.deletions = 1;
    before.hunks.push_back({1, 1, 1, 1, "@@ -1 +1 @@", {"-old", "+one"}});
    auto after = before;
    after.hunks[0].lines[1] = "+two";
    ASSERT_NE(ecs::diff_signature(before), ecs::diff_signature(after));
    ASSERT_NE(ecs::ReviewComponent::hunk_key(before.filePath, before.hunks[0]),
              ecs::ReviewComponent::hunk_key(after.filePath, after.hunks[0]));
    ASSERT_EQ(ecs::diff_signature(before), ecs::diff_signature(before));
}

TEST(export_keeps_the_original_code_and_revision) {
    ecs::ReviewComponent review;
    ecs::DiffHunk hunk{10, 2, 10, 2, "@@ -10,2 +10,2 @@", {" context", "-old", "+```new```"}};
    auto comment = ecs::comment_with_context({"wt", "code.txt", 11, "fix", 11, false}, hunk, "saved-head");
    ecs::begin_comment(review, "key", comment);
    ecs::commit_pending_comment(review);
    hunk.lines.back() = "+later mutation";
    auto markdown = ecs::build_review_markdown(review, "main");
    ASSERT_TRUE(markdown.find("Working tree at HEAD saved-head") != std::string::npos);
    ASSERT_TRUE(markdown.find("11: ```new```") != std::string::npos);
    ASSERT_TRUE(markdown.find("````text\n") != std::string::npos);
    ASSERT_TRUE(markdown.find("later mutation") == std::string::npos);
    auto old = ecs::comment_with_context({"commit-sha", "code.txt", 11, "old side", 11, true}, hunk, "");
    ASSERT_TRUE(old.revision.starts_with("commit-sha^ (parent)"));
    ASSERT_TRUE(old.codeContext.find("11: old") != std::string::npos);
    ASSERT_TRUE(old.codeContext.find("later mutation") == std::string::npos);
    review.comments.push_back(old);
    const std::string key = "/tmp/test_review_export_context";
    ASSERT_TRUE(review_store::save_review(key, review));
    ecs::ReviewComponent restored;
    review_store::load_review(key, restored);
    ASSERT_EQ(restored.comments.front().codeContext, comment.codeContext);
    ASSERT_EQ(restored.comments.front().revision, comment.revision);
    ASSERT_EQ(ecs::build_review_markdown(restored, "main"), ecs::build_review_markdown(review, "main"));
    std::filesystem::remove(review_store::review_path(key));
}

TEST(next_unreviewed_requires_explicit_metadata_review_and_wraps) {
    ecs::ReviewComponent review;
    ecs::FileDiff first;
    first.filePath = "a.cpp";
    first.hunks.push_back({1, 1, 1, 1, "@@ -1 +1 @@", {"-old", "+new"}});
    ecs::FileDiff metadata;
    metadata.filePath = "script.sh";
    metadata.oldMode = "100644";
    metadata.newMode = "100755";
    std::vector<ecs::FileDiff> files{first, metadata};
    ASSERT_TRUE(!ecs::file_reviewed(review, "wt", metadata));
    review.approvedHunks.insert("wt\n" + ecs::ReviewComponent::hunk_key(first.filePath, first.hunks.front()));
    ASSERT_TRUE(ecs::file_reviewed(review, "wt", first));
    review.reviewedFiles["wt\n" + first.filePath] = ecs::diff_signature(first);
    review.approvedHunks.clear();
    ASSERT_TRUE(!ecs::file_reviewed(review, "wt", first));
    review.approvedHunks.insert("wt\n" + ecs::ReviewComponent::hunk_key(first.filePath, first.hunks.front()));
    ASSERT_EQ(*ecs::next_unreviewed_file(review, "wt", files, {}, first.filePath), size_t{1});
    first.oldMode = "100644";
    first.newMode = "100755";
    ASSERT_TRUE(!ecs::file_reviewed(review, "wt", first));
    review.reviewedFiles["wt\n" + first.filePath] = ecs::diff_signature(first);
    ASSERT_TRUE(ecs::file_reviewed(review, "wt", first));
    first.oldMode.clear();
    first.newMode.clear();
    review.reviewedFiles["wt\n" + metadata.filePath] = ecs::diff_signature(metadata);
    ASSERT_TRUE(!ecs::next_unreviewed_file(review, "wt", files, {}, metadata.filePath));
    ASSERT_TRUE(!ecs::file_reviewed(review, "index", metadata));
    metadata.newMode = "120000";
    ASSERT_TRUE(!ecs::file_reviewed(review, "wt", metadata));
    files.front().hunks.front().lines.back() = "+changed again";
    ASSERT_EQ(*ecs::next_unreviewed_file(review, "wt", files, {}, metadata.filePath), size_t{0});
    review_files::Filter filter;
    filter.language = "Python";
    ASSERT_TRUE(!ecs::next_unreviewed_file(review, "wt", files, filter, ""));
}

TEST(unresolved_file_counts_are_target_scoped_and_include_old_rename_side) {
    ecs::ReviewComponent review;
    review.comments = {{"wt", "new.cpp", 1, "question"}, {"commit", "new.cpp", 1, "other target"},
        {"wt", "old.cpp", 1, "old-side rename", 1, true}, {"wt", "old.cpp", 1, "different file"},
        {"wt", "new.cpp", 2, "resolved", 2, false, true}};
    ASSERT_EQ(ecs::unresolved_file_count(review, "wt", "new.cpp", "old.cpp"), size_t{2});
    ASSERT_EQ(ecs::unresolved_file_count(review, "commit", "new.cpp"), size_t{1});
    review.comments.front().resolved = true;
    ASSERT_EQ(ecs::unresolved_file_count(review, "wt", "new.cpp"), size_t{0});
    ASSERT_EQ(ecs::unresolved_file_badge(review, "index", "new.cpp"), "");
}

TEST(typed_comments_preserve_drafts_edits_and_export) {
    ecs::ReviewComponent review;
    ecs::ReviewComponent::Comment draft{"wt", "typed.cpp", 1, "Why this value?"};
    draft.kind = ReviewCommentKind::Question;
    ecs::begin_comment(review, "typed-hunk", draft);
    const std::string repo = "/tmp/fh_review_typed_comment_test";
    ASSERT_TRUE(review_store::save_review(repo, review));
    ecs::ReviewComponent restored;
    review_store::load_review(repo, restored);
    ASSERT_EQ(restored.composingKind, ReviewCommentKind::Question);
    ecs::commit_pending_comment(restored);
    ASSERT_EQ(restored.comments.front().kind, ReviewCommentKind::Question);
    restored.editingComment = 0;
    restored.editingCommentText = "Please change this value";
    restored.editingCommentKind = ReviewCommentKind::Blocker;
    ASSERT_TRUE(review_store::save_review(repo, restored));
    ecs::ReviewComponent editing;
    review_store::load_review(repo, editing);
    ASSERT_EQ(editing.editingCommentKind, ReviewCommentKind::Blocker);
    ASSERT_TRUE(ecs::save_comment_edit(editing));
    ASSERT_EQ(editing.comments.front().kind, ReviewCommentKind::Blocker);
    ASSERT_TRUE(ecs::build_review_markdown(editing, "main").find("Type: Blocker") != std::string::npos);
    ASSERT_EQ(parse_review_comment_kind(""), ReviewCommentKind::Comment);
    ASSERT_EQ(parse_review_comment_kind("unknown-new-kind"), ReviewCommentKind::Comment);
    for (auto kind : {ReviewCommentKind::Comment, ReviewCommentKind::Question, ReviewCommentKind::Suggestion,
            ReviewCommentKind::Blocker, ReviewCommentKind::Nit})
        ASSERT_EQ(parse_review_comment_kind(review_comment_kind_label(kind)), kind);
    std::filesystem::remove(review_store::review_path(repo));
}

TEST(metadata_and_text_both_require_review) {
    ecs::ReviewComponent review;
    ecs::FileDiff file;
    file.filePath = "script.sh";
    file.oldMode = "100644";
    file.newMode = "100755";
    file.hunks.push_back({1, 1, 1, 1, "@@ -1 +1 @@", {"-old", "+new"}});
    auto key = "wt\n" + ecs::ReviewComponent::hunk_key(file.filePath, file.hunks.front());
    review.approvedHunks.insert(key);
    ASSERT_FALSE(ecs::file_reviewed(review, "wt", file));
    review.reviewedFiles["wt\n" + file.filePath] = ecs::diff_signature(file);
    ASSERT_TRUE(ecs::file_reviewed(review, "wt", file));
    review.approvedHunks.erase(key);
    ASSERT_FALSE(ecs::file_reviewed(review, "wt", file));
    review.approvedHunks.insert(key);
    file.oldPath = "old-script.sh";
    ASSERT_FALSE(ecs::file_reviewed(review, "wt", file));
}

TEST(review_verdicts_require_complete_scoped_progress_and_invalidate_on_binary_changes) {
    ecs::ReviewComponent review;
    ecs::FileDiff binary;
    binary.filePath = "image.bin";
    binary.isBinary = true;
    binary.oldObject = "old-object";
    binary.newObject = "new-object";
    std::vector<ecs::FileDiff> files{binary};
    ASSERT_TRUE(!ecs::review_progress(review, "wt", files).can_approve());
    review.reviewedFiles["wt\nimage.bin"] = ecs::diff_signature(binary);
    review.comments.push_back({"other-commit", "image.bin", 1, "unrelated"});
    ASSERT_TRUE(ecs::review_progress(review, "wt", files).can_approve());
    review.verdicts["wt"] = {ReviewVerdict::Approved, ecs::review_target_signature(files)};
    ASSERT_EQ(ecs::current_review_verdict(review, "wt", files), ReviewVerdict::Approved);
    ASSERT_EQ(ecs::current_review_verdict(review, "index", files), ReviewVerdict::InProgress);
    const std::string repo = "/tmp/fh_review_verdict_test";
    ASSERT_TRUE(review_store::save_review(repo, review));
    ecs::ReviewComponent restored;
    review_store::load_review(repo, restored);
    ASSERT_EQ(ecs::current_review_verdict(restored, "wt", files), ReviewVerdict::Approved);
    files.front().newObject = "updated-binary-object";
    ASSERT_TRUE(!ecs::file_reviewed(restored, "wt", files.front()));
    ASSERT_EQ(ecs::current_review_verdict(restored, "wt", files), ReviewVerdict::InProgress);
    files.front() = binary;
    restored.comments.push_back({"wt", "image.bin", 1, "followup"});
    ASSERT_TRUE(!ecs::review_progress(restored, "wt", files).can_approve());
    ASSERT_EQ(ecs::current_review_verdict(restored, "wt", files), ReviewVerdict::InProgress);
    std::filesystem::remove(review_store::review_path(repo));
}

TEST(comment_anchors_relocate_only_unique_matches_and_keep_original_evidence) {
    ecs::ReviewComponent::Comment comment{"wt", "file.cpp", 2, "question", 2};
    comment.codeContext = "1: before\n2: saved target\n3: after\n";
    ecs::FileDiff file;
    file.filePath = "file.cpp";
    file.hunks.push_back({1, 3, 1, 3, "", {" before", " saved target", " after"}});
    std::vector<ecs::FileDiff> files{file};
    ASSERT_EQ(review_anchor::locate(comment, &files).status, review_anchor::Status::Current);
    files.front().hunks.front().lines = {" before", " changed target", " after"};
    auto outdated = review_anchor::locate(comment, &files);
    ASSERT_EQ(outdated.status, review_anchor::Status::Outdated);
    ASSERT_EQ(outdated.saved, "saved target");
    ASSERT_EQ(outdated.current, "changed target");
    files.front().hunks.front().newStart = 8;
    files.front().hunks.front().lines = {" before", " saved target", " after"};
    auto moved = review_anchor::locate(comment, &files);
    ASSERT_EQ(moved.status, review_anchor::Status::Relocated);
    ASSERT_EQ(moved.line, 9);
    files.front().hunks.front().lines.push_back(" saved target");
    ASSERT_EQ(review_anchor::locate(comment, &files).status, review_anchor::Status::Unknown);
    ASSERT_EQ(review_anchor::locate(comment, nullptr).status, review_anchor::Status::Unknown);
    ASSERT_EQ(comment.line, 2);
    ASSERT_EQ(comment.codeContext, "1: before\n2: saved target\n3: after\n");
    comment.codeContext.clear();
    ASSERT_EQ(review_anchor::locate(comment, &files).status, review_anchor::Status::Unknown);
}

TEST(unresolved_filter_intersects_path_facets_and_ignores_other_targets) {
    ecs::ReviewComponent review;
    ecs::FileDiff a, b;
    a.filePath = "a.cpp";
    b.filePath = "b.py";
    std::vector<ecs::FileDiff> files{a, b};
    review.comments = {{"wt", "a.cpp", 1, "question"}, {"other-commit", "b.py", 1, "other target"}};
    review_files::Filter filter;
    filter.onlyUnresolved = true;
    ASSERT_EQ(ecs::visible_review_file_indices(files, filter, &review, "wt"), (std::vector<size_t>{0}));
    ASSERT_EQ(*ecs::next_unreviewed_file(review, "wt", files, filter, "b.py"), size_t{0});
    filter.language = "Python";
    ASSERT_TRUE(ecs::visible_review_file_indices(files, filter, &review, "wt").empty());
    filter.language.clear();
    review.comments.front().resolved = true;
    ASSERT_TRUE(ecs::visible_review_file_indices(files, filter, &review, "wt").empty());
    ASSERT_TRUE(ecs::visible_review_file_indices(files, filter, nullptr, "wt").empty());
    ASSERT_EQ(ecs::review_progress(review, "wt", files).total, size_t{2});
}

TEST(commit_review_queue_resumes_position_and_keeps_scope_isolated) {
    ecs::CommitEntry first{std::string(40, 'a'), "aaaaaaa", "First"};
    ecs::CommitEntry second{std::string(64, 'b'), "bbbbbbb", "Second"};
    ecs::ReviewComponent review;
    review.storageScope = "queue:base:target";
    review.queue = {{first, second}, 1, {first.hash}};
    const std::string repo = "/tmp/fh_review_queue_test";
    ASSERT_TRUE(review_store::save_review(repo, review));
    ecs::ReviewComponent restored;
    restored.storageScope = review.storageScope;
    review_store::load_review(repo, restored);
    ASSERT_EQ(restored.queue.position, size_t{1});
    ASSERT_EQ(restored.queue.commits[restored.queue.position].hash, second.hash);
    ASSERT_EQ(restored.queue.completed, review.queue.completed);
    auto reordered = ecs::refreshed_review_queue(restored.queue, {second, first});
    ASSERT_EQ(reordered.position, size_t{0});
    auto removed = ecs::refreshed_review_queue(restored.queue, {second});
    ASSERT_TRUE(removed.completed.empty());
    ASSERT_EQ(removed.position, size_t{0});
    ecs::ReviewComponent unrelated;
    unrelated.storageScope = "queue:other:target";
    review_store::load_review(repo, unrelated);
    ASSERT_TRUE(unrelated.queue.commits.empty());
    std::filesystem::remove(review_store::review_path(repo, review.storageScope));
}

TEST(queue_completion_stale_uses_loaded_default_commit_verdict) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review(std::string(40, 'a')));
    repo.reviewQueueScope = "queue:base:target";
    ecs::ReviewComponent review;
    review.queue.completed.insert(repo.selectedCommitHash());
    ecs::FileDiff file;
    file.filePath = "file.cpp";
    file.hunks.push_back({1, 1, 1, 1, "", {"-old", "+new"}});
    ecs::CommitDetailCache cache;
    cache.cachedCommitHash = repo.selectedCommitHash();
    cache.commitDetailDiff = {file};
    auto key = repo.selectedCommitHash() + "\n" + ecs::ReviewComponent::hunk_key(file.filePath, file.hunks.front());
    review.approvedHunks.insert(key);
    review.verdicts[repo.selectedCommitHash()] = {ReviewVerdict::Approved, ecs::review_target_signature(cache.commitDetailDiff)};
    ASSERT_FALSE(ecs::review_queue_completion_is_stale(review, repo, cache));
    review.approvedHunks.clear();
    ASSERT_TRUE(ecs::review_queue_completion_is_stale(review, repo, cache));
    review.approvedHunks.insert(key);
    review.verdicts.clear();
    ASSERT_TRUE(ecs::review_queue_completion_is_stale(review, repo, cache));
    review.verdicts[repo.selectedCommitHash()] = {ReviewVerdict::Approved, ecs::review_target_signature(cache.commitDetailDiff)};
    cache.commitDetailDiff.front().additions = 9;
    ASSERT_TRUE(ecs::review_queue_completion_is_stale(review, repo, cache));
    cache.commitDetailDiff.front() = file;
    navigation::open(repo, reading::review("parent:" + std::string(40, 'b') + ":" + repo.selectedCommitHash()));
    ASSERT_FALSE(ecs::review_queue_completion_is_stale(review, repo, cache));
    navigation::open(repo, reading::review(repo.selectedCommitHash()));
    review.approvedHunks.clear();
    cache.commitDetailError = "Unable to read patch";
    ASSERT_FALSE(ecs::review_queue_completion_is_stale(review, repo, cache));
    cache.commitDetailError.clear();
    cache.cachedCommitHash = std::string(40, 'c');
    ASSERT_FALSE(ecs::review_queue_completion_is_stale(review, repo, cache));
    cache.cachedCommitHash = repo.selectedCommitHash();
    review.verdicts[repo.selectedCommitHash()].verdict = ReviewVerdict::ChangesRequested;
    ASSERT_FALSE(ecs::review_queue_completion_is_stale(review, repo, cache));
    review.verdicts[repo.selectedCommitHash()].verdict = ReviewVerdict::Approved;
    review.queue.completed.clear();
    ASSERT_FALSE(ecs::review_queue_completion_is_stale(review, repo, cache));
    review.queue.completed.insert(repo.selectedCommitHash());
    ASSERT_TRUE(ecs::review_queue_completion_is_stale(review, repo, cache));
    std::promise<ecs::CommitPatch> pending;
    cache.patchFuture = async_work::Task<ecs::CommitPatch>(pending.get_future(), std::stop_source{});
    ASSERT_FALSE(ecs::review_queue_completion_is_stale(review, repo, cache));
}

int main() {
    afterhours::files::init("floatinghotel_test", "resources");
    printf("=== review_store tests ===\n");
    RUN_ALL_TESTS();
}
