#include "test_framework.h"
#include "../../src/ecs/async_git_refresh_system.h"
#include "../../src/git/repository_lock.h"
#include <filesystem>
#include <unistd.h>

TEST(closing_a_tab_cancels_its_refresh_reads_without_waiting_for_the_repository_lock) {
    char directory[] = "/tmp/fh-refresh-cancel.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    ASSERT_TRUE(git::git_run(path, {"init", "-q"}).success());
    afterhours::EntityCollection collection;
    afterhours::EntityHelper::set_default_collection(&collection);
    ecs::AsyncGitDataRefreshSystem refresh;
    auto& tab = afterhours::EntityHelper::createEntity();
    tab.addComponent<ecs::ActiveTab>();
    auto& repo = tab.addComponent<ecs::RepoComponent>();
    repo.repoPath = path;
    repo.refreshRequested = true;
    auto repositoryLock = git::repository_mutex(path);
    std::unique_lock held(*repositoryLock);
    refresh.for_each_with(tab, repo, 0);
    ASSERT_TRUE(repo.isRefreshing);
    tab.cleanup = true;
    afterhours::EntityHelper::cleanup();
    auto started = std::chrono::steady_clock::now();
    refresh.once(0);
    auto cleanupDuration = std::chrono::steady_clock::now() - started;
    auto barrier = async_work::launch([](std::stop_token) { return true; }, async_work::Priority::Background);
    auto ready = barrier.wait_for(std::chrono::seconds(2));
    held.unlock();
    barrier.get();
    afterhours::EntityHelper::set_default_collection(nullptr);
    std::filesystem::remove_all(path);
    ASSERT_TRUE(cleanupDuration < std::chrono::milliseconds(100));
    ASSERT_EQ(ready, std::future_status::ready);
}

TEST(refresh_cleanup_preserves_live_tabs_and_their_results) {
    char directory[] = "/tmp/fh-refresh-live.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    ASSERT_TRUE(git::git_run(path, {"init", "-q"}).success());
    afterhours::EntityCollection collection;
    afterhours::EntityHelper::set_default_collection(&collection);
    ecs::AsyncGitDataRefreshSystem refresh;
    auto& tab = afterhours::EntityHelper::createEntity();
    tab.addComponent<ecs::ActiveTab>();
    auto& repo = tab.addComponent<ecs::RepoComponent>();
    repo.repoPath = path;
    repo.refreshRequested = true;
    refresh.for_each_with(tab, repo, 0);
    ASSERT_TRUE(repo.isRefreshing);
    tab.removeComponent<ecs::ActiveTab>();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (repo.isRefreshing && std::chrono::steady_clock::now() < deadline) {
        refresh.once(0);
        refresh.for_each_with(tab, repo, 0);
        std::this_thread::yield();
    }
    bool completed = !repo.isRefreshing && repo.hasLoadedOnce && repo.filesError.empty();
    afterhours::EntityHelper::set_default_collection(nullptr);
    std::filesystem::remove_all(path);
    ASSERT_TRUE(completed);
}

int main() { RUN_ALL_TESTS(); }
