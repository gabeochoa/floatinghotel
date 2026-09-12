#pragma once

#include <chrono>
#include <filesystem>

#include "../../vendor/afterhours/src/core/system.h"
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

        if (repo.repoPath != watched_path_ || repo.repoVersion != watched_version_) {
            std::error_code ec;
            auto canon = std::filesystem::canonical(repo.repoPath, ec);
            if (ec) return;
            resolved_root_ = canon.string();
            common_git_dir_ = lfs_pointer::common_git_directory(repo.repoPath);
            std::vector<std::string> watchPaths{resolved_root_};
            auto commonCanon = std::filesystem::weakly_canonical(common_git_dir_, ec);
            if (!common_git_dir_.empty() && !ec && commonCanon != canon)
                watchPaths.push_back(commonCanon.string());
            watcher_.watch_many(watchPaths);
            watched_path_ = repo.repoPath;
            watched_version_ = repo.repoVersion;
            cooldown_until_ = clock::now() + COOLDOWN;
        }

        if (repo.refreshRequested || repo.isRefreshing) {
            cooldown_until_ = clock::now() + COOLDOWN;
            return;
        }

        if (clock::now() < cooldown_until_) return;

        auto events = watcher_.poll_events();
        if (!events.empty()) {
            auto scope = refresh_scope::Scope::None;
            for (const auto& event : events) {
                scope = refresh_scope::merge(scope, refresh_scope::classify(resolved_root_, common_git_dir_, event.path, event.mustRescan));
            }
            ++fired;
            repo.refreshScope = refresh_scope::merge(repo.refreshScope, scope);
            repo.refreshRequested = true;
            cooldown_until_ = clock::now() + COOLDOWN;
        }
    }

private:
    using clock = std::chrono::steady_clock;
    static constexpr auto COOLDOWN = std::chrono::milliseconds(1000);

    platform::FileWatcher watcher_;
    std::string watched_path_;
    std::string resolved_root_;
    std::filesystem::path common_git_dir_;
    unsigned watched_version_ = 0;
    clock::time_point cooldown_until_{};
};

} // namespace ecs
