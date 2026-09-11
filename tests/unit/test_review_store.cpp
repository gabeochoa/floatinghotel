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
    r.approvedHunks.insert("src/foo.cpp\n@@ -1 +1 @@");
    r.foldedHunks.insert("src/bar.h\n@@ -2 +2 @@");
    r.seenSig["src/foo.cpp"] = "1,2,3";
    r.baselineHead = "deadbeef";
    r.baselineDiffSig = "sig;";

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
    ASSERT_EQ(ecs::comment_location(r2.comments[1]), "src/bar.h:7-9 (old)");
    ASSERT_TRUE(r2.approvedHunks.count("src/foo.cpp\n@@ -1 +1 @@") == 1);
    ASSERT_TRUE(r2.foldedHunks.count("src/bar.h\n@@ -2 +2 @@") == 1);
    ASSERT_STREQ(r2.seenSig["src/foo.cpp"], "1,2,3");
    ASSERT_STREQ(r2.baselineHead, "deadbeef");

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
