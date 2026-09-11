// Unit tests for review_store: durable per-repo review persistence.
// Round-trips a populated ReviewComponent through save_review/load_review and
// checks a missing file is a clean no-op.

#include "test_framework.h"

#include <afterhours/src/plugins/files.h>

#include "../../src/review_store.h"
#include "../../src/ecs/components.h"

#include <filesystem>

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
    ecs::restore_draft_selection(repo, loaded);
    ASSERT_EQ(repo.selectedFilePath, "c.cpp");
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

int main() {
    afterhours::files::init("floatinghotel_test", "resources");
    printf("=== review_store tests ===\n");
    RUN_ALL_TESTS();
}
