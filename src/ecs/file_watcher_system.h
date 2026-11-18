#pragma once

#include <chrono>
#include <filesystem>

#include <afterhours/src/core/system.h>
#include "../platform/file_watcher.h"
#include "../util/lfs_pointer.h"
#include "components.h"

namespace ecs {

struct FileWatcherSystem : afterhours::System<RepoComponent> {

    bool disabled = false;
    // Times the watcher has requested a refresh. The E2E gate
    // `wait_for_file_change` arms on this, since headless ticks run far faster
    // than the wall clock the cooldown and FSEvents latency are measured in.
    unsigned fired = 0;

    void for_each_with(afterhours::Entity& entity,
                       RepoComponent& repo, float) override {
        if (disabled) return;
        if (!entity.has<ActiveTab>()) return;
        if (repo.repoPath.empty()) return;

        // Apply a finished watcher swap (canonical reads + stop/join ran
        // on a worker, not the switch path).
        if (swapFuture_.valid() &&
            swapFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto applied = swapFuture_.get();
            swapFuture_ = {};
            resolved_root_ = applied.root;
            common_git_dir_ = applied.commonGitDir;
            if (!swapTarget_.empty() && swapTarget_ != watched_path_) {
                watched_path_ = swapTarget_;
                launch_watch_swap(swapTarget_);
            }
            swapTarget_.clear();
        }
        if (repo.repoPath != watched_path_ || repo.repoVersion != watched_version_) {
            watched_path_ = repo.repoPath;
            watched_version_ = repo.repoVersion;
            pending_ = false;
            pending_scope_ = refresh_scope::Scope::None;
            cooldown_until_ = clock::now() + COOLDOWN;
            if (swapFuture_.valid()) swapTarget_ = repo.repoPath;  // coalesce rapid switches
            else launch_watch_swap(repo.repoPath);
        }

        // Drain every frame, even while a refresh is running or cooling down.
        // Events left in the watcher queue could be cleared by a re-watch
        // (tab/repo switch) and an amend landing mid-refresh was then lost.
        for (const auto& event : watcher_.poll_events()) {
            pending_scope_ = refresh_scope::merge(pending_scope_,
                refresh_scope::classify(resolved_root_, common_git_dir_, event.path, event.mustRescan));
            pending_ = true;
        }
        if (!pending_) return;
        if (repo.refreshRequested || repo.isRefreshing) {
            cooldown_until_ = clock::now() + COOLDOWN;
            return;
        }
        if (clock::now() < cooldown_until_) return;
        ++fired;
        repo.refreshScope = refresh_scope::merge(repo.refreshScope, pending_scope_);
        repo.refreshRequested = true;
        pending_ = false;
        pending_scope_ = refresh_scope::Scope::None;
        cooldown_until_ = clock::now() + COOLDOWN;
    }

private:
    using clock = std::chrono::steady_clock;
    static constexpr auto COOLDOWN = std::chrono::milliseconds(1000);

    struct WatchPaths {
        std::string root;
        std::filesystem::path commonGitDir;
    };
    void launch_watch_swap(const std::string& repoPath) {
        swapFuture_ = async_work::launch([this, repoPath](std::stop_token) {
            WatchPaths out;
            std::error_code ec;
            auto canon = std::filesystem::canonical(repoPath, ec);
            if (ec) return out;
            out.root = canon.string();
            out.commonGitDir = lfs_pointer::common_git_directory(repoPath);
            std::vector<std::string> watchPaths{out.root};
            auto commonCanon = std::filesystem::weakly_canonical(out.commonGitDir, ec);
            if (!out.commonGitDir.empty() && !ec && commonCanon != canon)
                watchPaths.push_back(commonCanon.string());
            watcher_.watch_many(watchPaths);
            return out;
        }, async_work::Priority::Background, WatchPaths{});
    }

    platform::FileWatcher watcher_;
    async_work::Task<WatchPaths> swapFuture_;
    std::string swapTarget_;
    std::string watched_path_;
    std::string resolved_root_;
    std::filesystem::path common_git_dir_;
    unsigned watched_version_ = 0;
    refresh_scope::Scope pending_scope_ = refresh_scope::Scope::None;
    bool pending_ = false;
    clock::time_point cooldown_until_{};
};

} // namespace ecs
