#include "test_framework.h"
#include "../../src/util/diff_reconcile.h"
#include "../../src/util/navigation.h"
#include "../../src/git/hunk_context.h"

static ecs::FileDiff file(const std::string& path, const std::string& line) {
    ecs::FileDiff value;
    value.filePath = path;
    value.additions = 1;
    value.hunks = {{10, 3, 10, 3, "@@ -10,3 +10,3 @@", {" ctx", line, " ctx2"}}};
    return value;
}

TEST(identical_refresh_is_not_a_change_and_keeps_every_identity) {
    std::vector<ecs::FileDiff> current{file("a.cpp", "+a"), file("b.cpp", "+b")};
    auto aId = current[0].renderIdentity, bId = current[1].renderIdentity, hunkId = current[0].hunks[0].renderIdentity;
    std::vector<ecs::FileDiff> next{file("a.cpp", "+a"), file("b.cpp", "+b")};
    ASSERT_FALSE(diff_reconcile::reconcile(current, next));
    ASSERT_EQ(current[0].renderIdentity, aId); ASSERT_EQ(current[1].renderIdentity, bId);
    ASSERT_EQ(current[0].hunks[0].renderIdentity, hunkId);
}

TEST(changed_refresh_only_replaces_the_changed_file_and_hunk) {
    std::vector<ecs::FileDiff> current{file("a.cpp", "+a"), file("b.cpp", "+b")};
    auto aId = current[0].renderIdentity, bHunk = current[1].hunks[0].renderIdentity;
    auto changed = file("b.cpp", "+b");
    changed.hunks.push_back({30, 3, 30, 3, "@@ -30,3 +30,3 @@", {" x", "+y", " z"}});
    std::vector<ecs::FileDiff> next{file("a.cpp", "+a"), changed};
    auto changedFileId = next[1].renderIdentity;
    ASSERT_TRUE(diff_reconcile::reconcile(current, next));
    ASSERT_EQ(current.size(), 2u);
    ASSERT_EQ(current[0].renderIdentity, aId);
    ASSERT_EQ(current[1].renderIdentity, changedFileId);
    ASSERT_EQ(current[1].hunks[0].renderIdentity, bHunk);
    ASSERT_TRUE(current[1].hunks[1].renderIdentity != bHunk);
}

TEST(expand_all_reveals_the_whole_gap_in_one_click) {
    ecs::RepoComponent repo;
    navigation::expand_hunk_context_all(repo, "k\nbelow");
    ASSERT_EQ(repo.workspace().document(repo.workspace().active_id())->contextLines.at("k\nbelow"), 4096);
    ecs::FileDiff value;
    value.hunks = {{27, 7, 27, 7}, {57, 7, 57, 7}};
    auto range = git::context_range(value, 0, false, 4096);
    ASSERT_EQ(range.count, 23); ASSERT_EQ(range.oldLine, 34);
    navigation::expand_hunk_context(repo, "step\nbelow");
    ASSERT_EQ(repo.workspace().document(repo.workspace().active_id())->contextLines.at("step\nbelow"), 20);
}

TEST(amended_tip_retargets_the_open_commit_without_a_new_visit) {
    ecs::RepoComponent repo;
    std::string oldHash(40, 'a'), newHash(40, 'b');
    navigation::open(repo, reading::review(oldHash, "a.cpp"), {}, reading::OpenMode::Keep);
    ASSERT_EQ(repo.selectedCommitHash(), oldHash);
    auto visits = repo.workspace().history().size();
    ASSERT_TRUE(navigation::follow_rewritten_tip(repo, oldHash, newHash));
    ASSERT_EQ(repo.selectedCommitHash(), newHash);
    ASSERT_EQ(repo.selectedFilePath(), "a.cpp");
    ASSERT_EQ(repo.workspace().history().size(), visits);
    ASSERT_FALSE(navigation::follow_rewritten_tip(repo, oldHash, newHash));
}

int main() { RUN_ALL_TESTS(); }
