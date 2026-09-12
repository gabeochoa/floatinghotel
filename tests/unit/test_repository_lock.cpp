#include "test_framework.h"
#include "../../src/git/repository_lock.h"
#include "../../src/git/git_runner.h"
#include <unistd.h>

TEST(aliases_and_linked_worktrees_share_a_lock_but_other_repos_do_not) {
    char directory[] = "/tmp/fh-locks.XXXXXX";
    const auto* root = mkdtemp(directory);
    ASSERT_TRUE(root != nullptr);
    auto repo = std::filesystem::path(root) / "repo";
    auto other = std::filesystem::path(root) / "other";
    auto linked = std::filesystem::path(root) / "linked";
    auto alias = std::filesystem::path(root) / "alias";
    ASSERT_TRUE(git::git_run("", {"init", "-q", repo.string()}).success());
    ASSERT_TRUE(git::git_run("", {"init", "-q", other.string()}).success());
    ASSERT_TRUE(git::git_run(repo.string(), {"-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit", "-qm", "Initial", "--allow-empty"}).success());
    ASSERT_TRUE(git::git_run(repo.string(), {"worktree", "add", "-qb", "linked", linked.string()}).success());
    std::filesystem::create_directory_symlink(repo, alias);
    std::filesystem::create_directory(repo / "nested");
    auto shared = git::repository_mutex(repo.string());
    ASSERT_EQ(shared, git::repository_mutex(alias.string()));
    ASSERT_EQ(shared, git::repository_mutex(linked.string()));
    ASSERT_EQ(shared, git::repository_mutex((repo / "nested").string()));
    ASSERT_NE(shared, git::repository_mutex(other.string()));
    std::unique_lock held(*shared);
    auto blocked = git::git_run_async(linked.string(), {"status", "--porcelain"});
    auto independent = git::git_run_async(other.string(), {"status", "--porcelain"});
    auto ready = independent.wait_for(std::chrono::seconds(3));
    auto waiting = blocked.wait_for(std::chrono::milliseconds(30));
    blocked.cancel();
    held.unlock();
    ASSERT_EQ(ready, std::future_status::ready);
    ASSERT_EQ(waiting, std::future_status::timeout);
    ASSERT_TRUE(independent.get().success());
    ASSERT_TRUE(blocked.get().raw.cancelled);
}

int main() { RUN_ALL_TESTS(); }
