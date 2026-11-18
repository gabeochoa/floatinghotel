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
    r.baselineCapturedAt = 1789300800;

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
    ASSERT_TRUE(r2.baselineSnapshot.has_value());
    ASSERT_EQ(r2.baseline, ecs::Baseline::Ready);
    ASSERT_EQ(r2.baselineCapturedAt, r.baselineCapturedAt);

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
    ecs::begin_comment(review, static_cast<ecs::ReviewComponent::CommentIndex>(0));
    ASSERT_EQ(review.composer, ecs::Composer::EditingExisting);
    review.composerDraft.text = "after\nsecond line";
    ASSERT_TRUE(ecs::save_comment_edit(review));
    ASSERT_EQ(review.comments.size(), 1u);
    ASSERT_EQ(review.comments.front().text, "after\nsecond line");
    ASSERT_EQ(ecs::comment_location(review.comments.front()), "file.cpp:12-14 (old)");
    ASSERT_TRUE(review.dirty());
    ASSERT_EQ(review.composer, ecs::Composer::Closed);
    ASSERT_EQ(review.editing_index(), -1);
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
    review.composerDraft.text = "Draft A";
    ecs::begin_comment(review, "hunk-b", {"abc", "b.cpp", 8, "", 9, true});
    review.composerDraft.text = "Draft B\nsecond line";
    review.comments.push_back({"wt", "c.cpp", 1, "queued text"});
    ecs::begin_comment(review, static_cast<ecs::ReviewComponent::CommentIndex>(0));
    review.composerDraft.text = "Unsaved edit";
    const std::string path = "/tmp/floatinghotel_draft_roundtrip";
    review_store::save_review(path, review);
    ecs::ReviewComponent loaded;
    review_store::load_review(path, loaded);
    // The unsaved edit is the active composer on load; the composing
    // draft survives alongside it in `drafts` (draft-on-dismiss).
    ASSERT_EQ(loaded.composer, ecs::Composer::EditingExisting);
    ASSERT_EQ(loaded.editing_index(), 0);
    ASSERT_EQ(loaded.composerDraft.text, "Unsaved edit");
    ASSERT_EQ(loaded.drafts.at("hunk-b").text, "Draft B\nsecond line");
    ASSERT_EQ(loaded.drafts.at("hunk-b").line, 8);
    ASSERT_TRUE(loaded.drafts.at("hunk-b").oldSide);
    ASSERT_EQ(loaded.drafts.at("hunk-a").text, "Draft A");
    ASSERT_EQ(loaded.comments.front().text, "queued text");
    ecs::RepoComponent repo;
    navigation::restore_draft(repo, loaded);
    ASSERT_EQ(repo.selectedFilePath(), "c.cpp");
    ecs::begin_comment(loaded, "hunk-a", {});
    ASSERT_EQ(loaded.composer, ecs::Composer::Composing);
    ASSERT_EQ(loaded.composerDraft.text, "Draft A");
    ecs::commit_pending_comment(loaded);
    ASSERT_EQ(loaded.composer, ecs::Composer::Closed);
    ASSERT_FALSE(loaded.drafts.contains("hunk-a"));
    ASSERT_TRUE(loaded.drafts.contains("hunk-b"));
    ASSERT_EQ(loaded.comments.size(), 2u);
    std::filesystem::remove(review_store::review_path(path));
}

TEST(deletion_is_persisted_and_failed_writes_keep_the_dirty_flag) {
    ecs::ReviewComponent review;
    review.comments.push_back({"wt", "code.cpp", 1, "remove me"});
    review.foldedHunks.insert("wt\ncode.cpp\nsignature");
    const std::string path = "/tmp/floatinghotel_delete_roundtrip";
    ASSERT_TRUE(review_store::save_review(path, review));
    ecs::erase_comment(review, 0);
    ASSERT_TRUE(review.dirty());
    ASSERT_TRUE(review.foldedHunks.empty());
    ASSERT_FALSE(review_store::persist_review("", review));
    ASSERT_TRUE(review.dirty());
    ASSERT_TRUE(review_store::persist_review(path, review));
    ASSERT_FALSE(review.dirty());
    ecs::ReviewComponent loaded;
    review_store::load_review(path, loaded);
    ASSERT_TRUE(loaded.comments.empty());
    std::filesystem::remove(review_store::review_path(path));
}

TEST(changing_feedback_scope_keeps_the_current_reading_panel_open) {
    ecs::ReviewComponent review;
    review.reviewing = true;
    ASSERT_TRUE(review_store::switch_review_scope("/tmp/fh-reading-panel", "new-scope", review, false));
    ASSERT_TRUE(review.reviewing);
    review.reviewing = false;
    ASSERT_TRUE(review_store::switch_review_scope("/tmp/fh-reading-panel", "another-scope", review, false));
    ASSERT_FALSE(review.reviewing);
}


// switch_review_scope now loads on a worker; drain it like the app does.
static void drain_review_load(ecs::ReviewComponent& review) {
    if (!review.reviewLoadFuture.valid()) return;
    auto loaded = review.reviewLoadFuture.get();
    review.reviewLoadFuture = {};
    review_store::apply_loaded_review(review, std::move(loaded));
}

TEST(review_state_is_isolated_by_branch_and_revision) {
    const std::string repo = "/tmp/floatinghotel_scoped_review";
    ecs::ReviewComponent review;
    ASSERT_TRUE(review_store::switch_review_scope(repo, "main@one", review));
    review.comments.push_back({"wt", "code.cpp", 1, "main note"});
    review.approvedHunks.insert("main approval");
    review.mark_dirty();
    ASSERT_TRUE(review_store::switch_review_scope(repo, "other@one", review));
    drain_review_load(review);
    ASSERT_TRUE(review.comments.empty());
    ASSERT_TRUE(review.approvedHunks.empty());
    review.comments.push_back({"wt", "code.cpp", 1, "other note"});
    review.mark_dirty();
    ASSERT_TRUE(review_store::switch_review_scope(repo, "main@one", review));
    drain_review_load(review);
    ASSERT_EQ(review.comments.front().text, "main note");
    ASSERT_TRUE(review.approvedHunks.contains("main approval"));
    ASSERT_TRUE(review_store::switch_review_scope(repo, "main@two", review));
    drain_review_load(review);
    ASSERT_TRUE(review.comments.empty());
    ASSERT_TRUE(review_store::switch_review_scope(repo, "other@one", review));
    drain_review_load(review);
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
    ASSERT_EQ(restored.composerDraft.kind, ReviewCommentKind::Question);
    ecs::commit_pending_comment(restored);
    ASSERT_EQ(restored.comments.front().kind, ReviewCommentKind::Question);
    ecs::begin_comment(restored, static_cast<ecs::ReviewComponent::CommentIndex>(0));
    restored.composerDraft.text = "Please change this value";
    restored.composerDraft.kind = ReviewCommentKind::Blocker;
    ASSERT_TRUE(review_store::save_review(repo, restored));
    ecs::ReviewComponent editing;
    review_store::load_review(repo, editing);
    ASSERT_EQ(editing.composer, ecs::Composer::EditingExisting);
    ASSERT_EQ(editing.composerDraft.kind, ReviewCommentKind::Blocker);
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
    ASSERT_EQ(review_anchor::locate(comment, &files).status, review_anchor::Status::Ambiguous);
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
    repo.reviewQueue.key = "queue:base:target";
    ecs::ReviewComponent review;
    review.queue.completed.insert(repo.selectedCommitHash());
    ecs::FileDiff file;
    file.filePath = "file.cpp";
    file.hunks.push_back({1, 1, 1, 1, "", {"-old", "+new"}});
    ecs::CommitDetailCache cache;
    cache.patch.key.commit = repo.selectedCommitHash();
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
    cache.patch.key.commit = std::string(40, 'c');
    ASSERT_FALSE(ecs::review_queue_completion_is_stale(review, repo, cache));
    cache.patch.key.commit = repo.selectedCommitHash();
    review.verdicts[repo.selectedCommitHash()].verdict = ReviewVerdict::ChangesRequested;
    ASSERT_FALSE(ecs::review_queue_completion_is_stale(review, repo, cache));
    review.verdicts[repo.selectedCommitHash()].verdict = ReviewVerdict::Approved;
    review.queue.completed.clear();
    ASSERT_FALSE(ecs::review_queue_completion_is_stale(review, repo, cache));
    review.queue.completed.insert(repo.selectedCommitHash());
    ASSERT_TRUE(ecs::review_queue_completion_is_stale(review, repo, cache));
    std::promise<ecs::CommitPatch> pending;
    cache.patch.request(cache.patch.key, {repo.repoPath, repo.workspace().location(), {}, repo.workspace().generation(), repo.dataGeneration},
        async_work::Task<ecs::CommitPatch>(pending.get_future(), std::stop_source{}));
    ASSERT_TRUE(cache.patch_loading());
    ASSERT_FALSE(ecs::review_queue_completion_is_stale(review, repo, cache));
}

TEST(dismissed_range_draft_reopens_and_saves_without_folding_code) {
    ecs::ReviewComponent review;
    ecs::begin_comment(review, "hunk", {"wt", "a.cpp", 5, "", 6, true});
    review.composerDraft.text = "Keep this range visible";
    ecs::dismiss_pending_comment(review);
    ASSERT_EQ(review.composer, ecs::Composer::Closed);
    ASSERT_TRUE(review.composing_key().empty());
    ASSERT_EQ(review.drafts.at("hunk").text, "Keep this range visible");
    ecs::begin_comment(review, "hunk", {});
    ASSERT_EQ(review.composer, ecs::Composer::Composing);
    ASSERT_EQ(review.composerDraft.line, 5);
    ASSERT_EQ(review.composerDraft.endLine, 6);
    ASSERT_TRUE(review.composerDraft.oldSide);
    ecs::commit_pending_comment(review);
    ASSERT_EQ(review.comments.size(), size_t{1});
    ASSERT_EQ(review.comments.front().text, "Keep this range visible");
    ASSERT_TRUE(review.foldedHunks.empty());
    ASSERT_TRUE(review.approvedHunks.empty());
    ASSERT_TRUE(review.drafts.empty());
}

TEST(baseline_phase_names_and_transitions) {
    ASSERT_STREQ(std::string(ecs::baseline_name(ecs::Baseline::None)), "None");
    ASSERT_STREQ(std::string(ecs::baseline_name(ecs::Baseline::Capturing)), "Capturing");
    ASSERT_STREQ(std::string(ecs::baseline_name(ecs::Baseline::Ready)), "Ready");
    ASSERT_STREQ(std::string(ecs::baseline_name(ecs::Baseline::Failed)), "Failed");
    ecs::ReviewComponent review;
    ASSERT_EQ(review.baseline, ecs::Baseline::None);
    ASSERT_FALSE(review.baselineSnapshot.has_value());
    review.begin_baseline_capture();
    ASSERT_EQ(review.baseline, ecs::Baseline::Capturing);
    review.finish_baseline_capture("/tmp/base.cbor", "deadbeef", 123);
    ASSERT_EQ(review.baseline, ecs::Baseline::Ready);
    ASSERT_EQ(review.baselineSnapshot.value(), "/tmp/base.cbor");
    ASSERT_STREQ(review.baselineHead, "deadbeef");
    ASSERT_EQ(review.baselineCapturedAt, 123);
    // A failed re-capture keeps the held snapshot.
    review.begin_baseline_capture();
    review.fail_baseline("git failed");
    ASSERT_EQ(review.baseline, ecs::Baseline::Failed);
    ASSERT_STREQ(review.snapshotError, "git failed");
    ASSERT_TRUE(review.baselineSnapshot.has_value());
    // A successful compare recovers Failed -> Ready.
    review.finish_baseline_compare();
    ASSERT_EQ(review.baseline, ecs::Baseline::Ready);
}

TEST(composer_phase_names_targets_and_draft_on_dismiss) {
    ASSERT_STREQ(std::string(ecs::composer_name(ecs::Composer::Closed)), "Closed");
    ASSERT_STREQ(std::string(ecs::composer_name(ecs::Composer::Composing)), "Composing");
    ASSERT_STREQ(std::string(ecs::composer_name(ecs::Composer::EditingExisting)), "EditingExisting");
    ecs::ReviewComponent review;
    ASSERT_EQ(review.composer, ecs::Composer::Closed);
    ecs::begin_comment(review, "hunk-1", {"wt", "a.cpp", 3, "", 4, false});
    ASSERT_EQ(review.composer, ecs::Composer::Composing);
    ASSERT_TRUE(std::holds_alternative<ecs::ReviewComponent::HunkKey>(review.composerTarget));
    ASSERT_EQ(review.composing_key(), "hunk-1");
    ASSERT_EQ(review.editing_index(), -1);
    review.composerDraft.text = "draft text";
    ecs::dismiss_pending_comment(review);
    ASSERT_EQ(review.composer, ecs::Composer::Closed);
    ASSERT_EQ(review.drafts.at("hunk-1").text, "draft text");
    // Editing an existing comment targets its index; commit writes back.
    review.comments.push_back({"wt", "b.cpp", 7, "original"});
    ecs::begin_comment(review, static_cast<ecs::ReviewComponent::CommentIndex>(0));
    ASSERT_EQ(review.composer, ecs::Composer::EditingExisting);
    ASSERT_TRUE(std::holds_alternative<ecs::ReviewComponent::CommentIndex>(review.composerTarget));
    ASSERT_EQ(review.editing_index(), 0);
    ASSERT_TRUE(review.composing_key().empty());
    // The preserved draft is still waiting in drafts, not lost.
    ASSERT_EQ(review.drafts.at("hunk-1").text, "draft text");
    review.composerDraft.text = "revised";
    ecs::commit_pending_comment(review);
    ASSERT_EQ(review.composer, ecs::Composer::Closed);
    ASSERT_EQ(review.comments.front().text, "revised");
    // Dismissing an edit discards the buffer, original untouched.
    ecs::begin_comment(review, static_cast<ecs::ReviewComponent::CommentIndex>(0));
    review.composerDraft.text = "discard me";
    ecs::dismiss_pending_comment(review);
    ASSERT_EQ(review.comments.front().text, "revised");
    // Erasing a comment before the edit target shifts the index.
    review.comments.push_back({"wt", "c.cpp", 1, "second"});
    ecs::begin_comment(review, static_cast<ecs::ReviewComponent::CommentIndex>(1));
    ecs::erase_comment(review, 0);
    ASSERT_EQ(review.composer, ecs::Composer::EditingExisting);
    ASSERT_EQ(review.editing_index(), 0);
    ecs::erase_comment(review, 0);
    ASSERT_EQ(review.composer, ecs::Composer::Closed);
}

TEST(review_load_failure_preserves_memory_and_disk) {
    const std::string repo = "/fixture/failure-aware-review";
    ecs::ReviewComponent review;
    review.comments.push_back({"wt", "keep.cpp", 4, "keep this"});
    for (const std::string& contents : {std::string(R"({"schema_version":99})"),
            std::string(R"({"comments":[{"text":"new"}],"approved_hunks":5})"),
            std::string(R"({"repo_path":"/another/repository"})")}) {
        ASSERT_TRUE(afterhours::files::write_string_atomic(review_store::review_path(repo), contents));
        review_store::load_review(repo, review);
        ASSERT_EQ(review.storePhase, ecs::ReviewStorePhase::LoadFailed);
        ASSERT_FALSE(review.storeError.empty());
        ASSERT_EQ(review.comments.size(), 1u);
        ASSERT_EQ(review.comments[0].text, std::string("keep this"));
        ASSERT_FALSE(review_store::save_review(repo, review));
        ASSERT_EQ(afterhours::files::read_string(review_store::review_path(repo)).value(), contents);
    }
    std::filesystem::remove(review_store::review_path(repo));
    review_store::load_review(repo, review);
    ASSERT_NE(review.storePhase, ecs::ReviewStorePhase::LoadFailed);
    ASSERT_TRUE(review.storeError.empty());
    ASSERT_TRUE(review_store::save_review(repo, review));
    std::filesystem::remove(review_store::review_path(repo));
}

TEST(disjoint_feedback_roundtrips_without_including_gaps) {
    const std::string path = "/tmp/review-disjoint-fixture";
    ecs::ReviewComponent review;
    ecs::ReviewComponent::Comment comment{"wt", "file.cpp", 2, "Update these together", 2, false};
    comment.ranges = {{2, 3, false}, {20, 20, false}, {7, 7, true}};
    ecs::begin_comment(review, "wt\nfile.cpp", comment);
    ecs::commit_pending_comment(review);
    ASSERT_TRUE(review_store::save_review(path, review));
    ecs::ReviewComponent loaded;
    review_store::load_review(path, loaded);
    ASSERT_EQ(loaded.comments[0].ranges, comment.ranges);
    const auto location = ecs::comment_location(loaded.comments[0]);
    ASSERT_TRUE(location.find("after 2-3, after 20, before 7") != std::string::npos);
    ASSERT_TRUE(ecs::build_review_markdown(loaded, "main").find(location) != std::string::npos);
    const auto moved = path + "-moved";
    std::filesystem::remove(review_store::review_path(moved));
    ASSERT_TRUE(review_store::copy_repository_reviews(path, moved).empty());
    ecs::ReviewComponent migrated;
    review_store::load_review(moved, migrated);
    ASSERT_EQ(migrated.comments[0].ranges, comment.ranges);
    ASSERT_TRUE(std::filesystem::exists(review_store::review_path(path)));
    std::filesystem::remove(review_store::review_path(path));
    std::filesystem::remove(review_store::review_path(moved));
}

TEST(review_store_migrates_legacy_keys_without_deleting_original) {
    const std::string repo = "/tmp/legacy-review-identity";
    const auto current = std::filesystem::path(review_store::review_path(repo));
    std::filesystem::remove(current);
    const auto legacy = current.parent_path() / (std::to_string(std::hash<std::string>{}(repo)) + ".json");
    ASSERT_TRUE(afterhours::files::write_string_atomic(legacy, R"({"repo_path":"/tmp/legacy-review-identity","comments":[{"file":"a.cpp","line":1,"text":"Keep me"}]})"));
    ecs::ReviewComponent loaded;
    review_store::load_review(repo, loaded);
    ASSERT_NE(loaded.storePhase, ecs::ReviewStorePhase::LoadFailed);
    ASSERT_TRUE(loaded.storeError.empty());
    ASSERT_EQ(loaded.comments.size(), 1u);
    ASSERT_TRUE(std::filesystem::exists(legacy));
    ASSERT_TRUE(std::filesystem::exists(current));
    std::filesystem::remove(legacy);
    std::filesystem::remove(current);
}

TEST(review_store_phase_name_covers_all_phases) {
    ASSERT_STREQ(std::string(ecs::review_store_phase_name(ecs::ReviewStorePhase::Unloaded)), "Unloaded");
    ASSERT_STREQ(std::string(ecs::review_store_phase_name(ecs::ReviewStorePhase::Loading)), "Loading");
    ASSERT_STREQ(std::string(ecs::review_store_phase_name(ecs::ReviewStorePhase::Ready)), "Ready");
    ASSERT_STREQ(std::string(ecs::review_store_phase_name(ecs::ReviewStorePhase::Dirty)), "Dirty");
    ASSERT_STREQ(std::string(ecs::review_store_phase_name(ecs::ReviewStorePhase::Saving)), "Saving");
    ASSERT_STREQ(std::string(ecs::review_store_phase_name(ecs::ReviewStorePhase::SaveFailed)), "SaveFailed");
    ASSERT_STREQ(std::string(ecs::review_store_phase_name(ecs::ReviewStorePhase::LoadFailed)), "LoadFailed");
}

TEST(review_store_dirty_save_and_save_failed_transitions) {
    ecs::ReviewComponent review;
    ASSERT_EQ(review.storePhase, ecs::ReviewStorePhase::Unloaded);
    ASSERT_FALSE(review.dirty());
    review_store::load_review("/tmp/fh_phase_missing_repo", review);
    ASSERT_EQ(review.storePhase, ecs::ReviewStorePhase::Ready);
    review.comments.push_back({"wt", "a.cpp", 1, "note"});
    review.mark_dirty();
    ASSERT_EQ(review.storePhase, ecs::ReviewStorePhase::Dirty);
    ASSERT_TRUE(review.dirty());
    const std::string path = "/tmp/fh_phase_save";
    ASSERT_TRUE(review_store::persist_review(path, review));
    ASSERT_EQ(review.storePhase, ecs::ReviewStorePhase::Ready);
    ASSERT_FALSE(review.dirty());
    // A failed write lands in SaveFailed and keeps the work dirty.
    review.mark_dirty();
    ASSERT_FALSE(review_store::persist_review("", review));
    ASSERT_EQ(review.storePhase, ecs::ReviewStorePhase::SaveFailed);
    ASSERT_TRUE(review.dirty());
    ASSERT_FALSE(review.storeError.empty());
    // Editing while SaveFailed does not mask the failure.
    review.mark_dirty();
    ASSERT_EQ(review.storePhase, ecs::ReviewStorePhase::SaveFailed);
    // The retry loop recovers to Ready on the next successful write.
    ASSERT_TRUE(review_store::persist_review(path, review));
    ASSERT_EQ(review.storePhase, ecs::ReviewStorePhase::Ready);
    std::filesystem::remove(review_store::review_path(path));
}

TEST(review_store_load_failed_retry_recovers_and_unblocks_saves) {
    const std::string repo = "/tmp/fh_phase_load_failed";
    std::filesystem::remove(review_store::review_path(repo));
    ecs::ReviewComponent review;
    review.comments.push_back({"wt", "keep.cpp", 4, "keep this"});
    ASSERT_TRUE(afterhours::files::write_string_atomic(review_store::review_path(repo), R"({"schema_version":99})"));
    review_store::load_review(repo, review);
    ASSERT_EQ(review.storePhase, ecs::ReviewStorePhase::LoadFailed);
    ASSERT_FALSE(review_store::save_review(repo, review));
    // Retry while the file is still corrupt stays LoadFailed and blocked.
    ASSERT_FALSE(review_store::retry_load(repo, review));
    ASSERT_EQ(review.storePhase, ecs::ReviewStorePhase::LoadFailed);
    ASSERT_FALSE(review_store::save_review(repo, review));
    ASSERT_EQ(review.comments.front().text, std::string("keep this"));
    // Removing the corrupt file is the recovery: preserved work becomes
    // Dirty (not silently Ready) so persist can now write it.
    std::filesystem::remove(review_store::review_path(repo));
    ASSERT_TRUE(review_store::retry_load(repo, review));
    ASSERT_EQ(review.storePhase, ecs::ReviewStorePhase::Dirty);
    ASSERT_TRUE(review.dirty());
    ASSERT_TRUE(review_store::persist_review(repo, review));
    ASSERT_EQ(review.storePhase, ecs::ReviewStorePhase::Ready);
    ecs::ReviewComponent loaded;
    review_store::load_review(repo, loaded);
    ASSERT_EQ(loaded.storePhase, ecs::ReviewStorePhase::Ready);
    ASSERT_EQ(loaded.comments.front().text, std::string("keep this"));
    std::filesystem::remove(review_store::review_path(repo));
    // A repaired (valid) file retries straight to Ready.
    ASSERT_TRUE(afterhours::files::write_string_atomic(review_store::review_path(repo), R"({"schema_version":99})"));
    review_store::load_review(repo, review);
    ASSERT_EQ(review.storePhase, ecs::ReviewStorePhase::LoadFailed);
    ASSERT_TRUE(afterhours::files::write_string_atomic(review_store::review_path(repo),
        R"({"schema_version":2,"repo_path":"/tmp/fh_phase_load_failed","comments":[{"scope":"wt","file":"fixed.cpp","line":1,"text":"from disk"}]})"));
    ASSERT_TRUE(review_store::retry_load(repo, review));
    ASSERT_EQ(review.storePhase, ecs::ReviewStorePhase::Ready);
    ASSERT_EQ(review.comments.front().file, std::string("fixed.cpp"));
    std::filesystem::remove(review_store::review_path(repo));
}

TEST(review_store_async_load_names_loading_then_ready) {
    const std::string repo = "/tmp/fh_phase_async_load";
    ecs::ReviewComponent review;
    ASSERT_TRUE(review_store::switch_review_scope(repo, "scope-a", review));
    ASSERT_EQ(review.storePhase, ecs::ReviewStorePhase::Loading);
    drain_review_load(review);
    ASSERT_EQ(review.storePhase, ecs::ReviewStorePhase::Ready);
    std::filesystem::remove(review_store::review_path(repo, "scope-a"));
}

TEST(disjoint_feedback_does_not_change_plain_selection_and_expires_on_refresh) {
    ecs::RepoComponent repo;
    navigation::set_selection(repo, reading::CodeSelection{{"a.cpp", reading::DiffSide::After, 1, 1}, {"a.cpp", reading::DiffSide::After, 1, 3}});
    const auto original = repo.workspace().document(repo.workspace().active_id())->selection;
    ASSERT_TRUE(navigation::toggle_feedback_line(repo, {"a.cpp", reading::DiffSide::After, 2, 6}));
    ASSERT_TRUE(navigation::toggle_feedback_line(repo, {"a.cpp", reading::DiffSide::After, 12, 2}));
    auto* document = repo.workspace().document(repo.workspace().active_id());
    ASSERT_EQ(document->feedbackLines.size(), 2u);
    ASSERT_EQ(document->selection, original);
    ASSERT_TRUE(navigation::toggle_feedback_line(repo, {"a.cpp", reading::DiffSide::After, 2, 1}));
    ASSERT_EQ(document->feedbackLines.size(), 1u);
    ++repo.dataGeneration;
    ASSERT_TRUE(navigation::toggle_feedback_line(repo, {"a.cpp", reading::DiffSide::Before, 3, 1}));
    ASSERT_EQ(document->feedbackLines.size(), 1u);
    ASSERT_EQ(document->feedbackLines[0].line, 3);
}

int main() {
    afterhours::files::init("floatinghotel_test", "resources");
    printf("=== review_store tests ===\n");
    RUN_ALL_TESTS();
}
