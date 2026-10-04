#include "test_framework.h"
#include "../../src/util/follow_up_review.h"

static ecs::FileDiff file(std::string path, std::vector<std::string> lines) {
    ecs::FileDiff result;
    result.filePath = std::move(path);
    ecs::DiffHunk hunk;
    hunk.oldStart = hunk.newStart = 1;
    hunk.oldCount = hunk.newCount = static_cast<int>(lines.size());
    hunk.lines = std::move(lines);
    result.hunks.push_back(std::move(hunk));
    return result;
}

TEST(combines_changed_files_and_unresolved_locations_without_duplicate_work) {
    std::vector<ecs::FileDiff> current{file("a.cpp", {" value"}), file("b.cpp", {" value"})};
    ecs::ReviewComponent::Comment comment{"wt", "a.cpp", 1, "first", 1};
    comment.codeContext = "1: value\n";
    auto other = comment;
    other.text = "second";
    auto resolved = comment;
    resolved.file = "done.cpp";
    resolved.resolved = true;
    auto historical = comment;
    historical.scope = "commit";
    const auto result = follow_up_review::build(current, {comment, other, resolved, historical}, current);
    ASSERT_EQ(result.size(), 2u);
    ASSERT_EQ(result[0].path, "a.cpp");
    ASSERT_EQ(result[0].comments, 2u);
    ASSERT_TRUE(result[0].changed);
    ASSERT_EQ(result[0].status, review_anchor::Status::Current);
    ASSERT_EQ(result[1].path, "b.cpp");
    ASSERT_EQ(result[1].comments, 0u);
    ASSERT_TRUE(result[1].changed);
    ASSERT_TRUE(follow_up_review::build({}, {resolved, historical}, current).empty());
}

TEST(retains_outdated_ambiguous_and_unlocated_warnings) {
    std::vector<ecs::FileDiff> current{file("a.cpp", {" different"}), file("b.cpp", {" different", " saved", " saved"})};
    ecs::ReviewComponent::Comment a{"wt", "a.cpp", 1, "note", 1};
    a.codeContext = "1: saved\n";
    auto b = a;
    b.file = "b.cpp";
    auto c = a;
    c.file = "missing.cpp";
    auto result = follow_up_review::build({}, {c, b, a}, current);
    ASSERT_EQ(result.size(), 3u);
    ASSERT_EQ(result[0].status, review_anchor::Status::Outdated);
    ASSERT_EQ(result[1].status, review_anchor::Status::Ambiguous);
    ASSERT_EQ(result[2].status, review_anchor::Status::Unknown);
    ASSERT_TRUE(follow_up_review::label(result[0]).starts_with("Outdated"));
    ASSERT_TRUE(follow_up_review::label(result[1]).starts_with("Ambiguous"));
    ASSERT_TRUE(follow_up_review::label(result[2]).starts_with("Unlocated"));
}

TEST(renamed_old_side_comments_use_the_current_review_file_identity) {
    auto renamed = file("new.cpp", {" saved", " saved two"});
    renamed.oldPath = "old.cpp";
    ecs::ReviewComponent::Comment comment{"wt", "old.cpp", 1, "note", 2, true};
    comment.codeContext = "1: saved\n2: saved two\n";
    auto result = follow_up_review::build({renamed}, {comment}, {renamed});
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].path, "new.cpp");
    ASSERT_EQ(result[0].endLine, 2);
    ASSERT_TRUE(result[0].oldSide && result[0].changed);
    ASSERT_EQ(result[0].status, review_anchor::Status::Current);
}

TEST(large_lists_keep_deterministic_unique_positions) {
    std::vector<ecs::ReviewComponent::Comment> comments;
    for (int i = 0; i < 2000; ++i) {
        ecs::ReviewComponent::Comment item{"wt", "file.cpp", i + 1, "note", i + 1};
        comments.push_back(item);
        comments.push_back(item);
    }
    const auto result = follow_up_review::build({}, comments, {});
    ASSERT_EQ(result.size(), 2000u);
    for (size_t i = 0; i < result.size(); ++i) {
        ASSERT_EQ(result[i].line, static_cast<int>(i + 1));
        ASSERT_EQ(result[i].comments, 2u);
    }
}

int main() { RUN_ALL_TESTS(); }

TEST(commit_carries_comments_on_its_changed_lines_by_content) {
    ecs::FileDiff commit;
    commit.filePath = "a.cpp";
    ecs::DiffHunk hunk;
    hunk.oldStart = 5; hunk.newStart = 7;
    hunk.oldCount = 3; hunk.newCount = 4;
    hunk.lines = {" keep", "-old", "+new", "+dup", " tail"};
    ecs::DiffHunk dups;
    dups.oldStart = 20; dups.newStart = 22;
    dups.oldCount = dups.newCount = 2;
    dups.lines = {" twin", " twin"};
    commit.hunks = {hunk, dups};
    ecs::ReviewComponent::Comment moved{"wt", "a.cpp", 3, "rename this", 4};
    moved.codeContext = "3: new\n4: dup\n";
    auto context = moved;
    context.line = context.endLine = 1;
    context.codeContext = "1: keep\n";
    auto ambiguous = context;
    ambiguous.codeContext = "1: twin\n";
    auto resolved = moved;
    resolved.resolved = true;
    std::vector<ecs::ReviewComponent::Comment> comments{moved, context, ambiguous, resolved};
    const std::vector files{commit};
    const auto carried = review_anchor::carry_to_commit(comments, "abc123", files);
    ASSERT_EQ(carried.moved, 1u);
    ASSERT_EQ(carried.ambiguous, 1u);
    ASSERT_EQ(comments[0].scope, "abc123");
    ASSERT_EQ(comments[0].line, 8);
    ASSERT_EQ(comments[0].endLine, 9);
    ASSERT_TRUE(comments[0].revision.starts_with("abc123; hunk "));
    ASSERT_EQ(review_anchor::locate(comments[0], &files).status, review_anchor::Status::Current);
    ASSERT_EQ(comments[1].scope, "wt");
    ASSERT_EQ(comments[2].scope, "wt");
    ASSERT_EQ(comments[3].scope, "wt");
}
