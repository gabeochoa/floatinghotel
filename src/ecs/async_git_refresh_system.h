#pragma once

#include <chrono>
#include <future>
#include <optional>
#include <unordered_map>

#include "../../vendor/afterhours/src/core/system.h"
#include "../git/git_parser.h"
#include "../git/git_runner.h"
#include "components.h"

namespace ecs {

struct AsyncGitDataRefreshSystem : afterhours::System<RepoComponent> {

    void for_each_with(afterhours::Entity& entity,
                       RepoComponent& repo, float) override {

        auto id = entity.id;

        // Phase 1: kick off async operations for any tab that requests refresh
        if (repo.refreshRequested && !repo.isRefreshing) {
            if (repo.repoPath.empty()) {
                repo.refreshRequested = false;
                return;
            }

            repo.refreshRequested = false;
            repo.isRefreshing = true;

            const std::string path = repo.repoPath;
            auto& pf = pending_[id];
            pf.status   = git::git_status_async(path);
            pf.log      = git::git_log_async(path, 100, 0);
            pf.diff     = git::git_diff_async(path);
            pf.branches = git::git_branch_list_async(path);
            pf.head     = git::git_rev_parse_head_async(path);
        }

        if (!repo.isRefreshing) return;

        auto it = pending_.find(id);
        if (it == pending_.end()) {
            repo.isRefreshing = false;
            return;
        }
        auto& pf = it->second;

        // Phase 2: poll each future (non-blocking)
        using namespace std::chrono_literals;

        if (pf.status &&
            pf.status->wait_for(0s) == std::future_status::ready) {
            auto result = pf.status->get();
            pf.status.reset();
            if (result.success()) {
                auto parsed = git::parse_status(result.stdout_str());
                repo.currentBranch  = parsed.branchName;
                repo.isDetachedHead = parsed.isDetachedHead;
                repo.aheadCount     = parsed.aheadCount;
                repo.behindCount    = parsed.behindCount;
                repo.stagedFiles    = std::move(parsed.stagedFiles);
                repo.unstagedFiles  = std::move(parsed.unstagedFiles);
                repo.untrackedFiles = std::move(parsed.untrackedFiles);
                repo.isDirty = !repo.stagedFiles.empty() ||
                               !repo.unstagedFiles.empty() ||
                               !repo.untrackedFiles.empty();
            }
        }

        if (pf.log &&
            pf.log->wait_for(0s) == std::future_status::ready) {
            auto result = pf.log->get();
            pf.log.reset();
            if (result.success()) {
                repo.commitLog = git::parse_log(result.stdout_str());
                repo.commitLogLoaded =
                    static_cast<int>(repo.commitLog.size());
                repo.commitLogHasMore = (repo.commitLogLoaded >= 100);
            }
        }

        if (pf.diff &&
            pf.diff->wait_for(0s) == std::future_status::ready) {
            auto result = pf.diff->get();
            pf.diff.reset();
            if (result.success()) {
                repo.currentDiff = git::parse_diff(result.stdout_str());
            }
        }

        if (pf.branches &&
            pf.branches->wait_for(0s) == std::future_status::ready) {
            auto result = pf.branches->get();
            pf.branches.reset();
            if (result.success()) {
                repo.branches =
                    git::parse_branch_list(result.stdout_str());
            }
        }

        if (pf.head &&
            pf.head->wait_for(0s) == std::future_status::ready) {
            auto result = pf.head->get();
            pf.head.reset();
            if (result.success()) {
                repo.headCommitHash = result.stdout_str();
                while (!repo.headCommitHash.empty() &&
                       (repo.headCommitHash.back() == '\n' ||
                        repo.headCommitHash.back() == '\r')) {
                    repo.headCommitHash.pop_back();
                }
            }
        }

        // Phase 3: check if all operations completed
        if (!pf.status && !pf.log && !pf.diff &&
            !pf.branches && !pf.head) {
            repo.isRefreshing = false;
            repo.hasLoadedOnce = true;
            pending_.erase(it);
        }
    }

private:
    struct PendingFutures {
        std::optional<std::future<git::GitResult>> status;
        std::optional<std::future<git::GitResult>> log;
        std::optional<std::future<git::GitResult>> diff;
        std::optional<std::future<git::GitResult>> branches;
        std::optional<std::future<git::GitResult>> head;
    };

    std::unordered_map<afterhours::EntityID, PendingFutures> pending_;
};

}  // namespace ecs
