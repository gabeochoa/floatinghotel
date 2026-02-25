#pragma once

#include <chrono>
#include <filesystem>

#include "../../vendor/afterhours/src/core/system.h"
#include "../platform/file_watcher.h"
#include "components.h"

namespace ecs {

struct FileWatcherSystem : afterhours::System<RepoComponent> {

    void for_each_with(afterhours::Entity& entity,
                       RepoComponent& repo, float) override {
        if (!entity.has<ActiveTab>()) return;
        if (repo.repoPath.empty()) return;

        std::error_code ec;
        auto canon = std::filesystem::canonical(repo.repoPath, ec);
        if (ec) return;
        std::string resolved = canon.string();

        if (resolved != watched_path_) {
            watcher_.watch(resolved);
            watched_path_ = resolved;
            cooldown_until_ = clock::now() + COOLDOWN;
        }

        if (repo.refreshRequested || repo.isRefreshing) {
            cooldown_until_ = clock::now() + COOLDOWN;
            return;
        }

        if (clock::now() < cooldown_until_) return;

        if (watcher_.poll_changed()) {
            repo.refreshRequested = true;
            cooldown_until_ = clock::now() + COOLDOWN;
        }
    }

private:
    using clock = std::chrono::steady_clock;
    static constexpr auto COOLDOWN = std::chrono::milliseconds(1000);

    platform::FileWatcher watcher_;
    std::string watched_path_;
    clock::time_point cooldown_until_{};
};

} // namespace ecs
