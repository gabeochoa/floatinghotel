#pragma once

#include <chrono>
#include <future>
#include <optional>

#include "../../vendor/afterhours/src/core/system.h"
#include "../git/git_parser.h"
#include "../git/git_runner.h"
#include "components.h"

namespace ecs {

// AsyncGitDataRefreshSystem: Non-blocking replacement for
// GitDataRefreshSystem.
//
// When RepoComponent::refreshRequested is set, this system launches all
// git queries (status, log, diff, branch-list, rev-parse HEAD) on
// background threads via std::async.  Each frame it polls the
// outstanding futures with a zero-timeout wait_for(); when a result is
// ready it is consumed and written into the RepoComponent on the main
// thread.  Once every pending operation has completed,
// RepoComponent::isRefreshing is cleared.
//
// The original synchronous GitDataRefreshSystem is preserved alongside
// this one -- swap the registration in main.cpp to choose which to use.
struct AsyncGitDataRefreshSystem : afterhours::System<RepoComponent> {

    void for_each_with(afterhours::Entity& entity,
                       RepoComponent& repo, float) override {

        if (!entity.has<ActiveTab>()) return;

        // ---- Phase 1: kick off async operations ----
        if (repo.refreshRequested && !repo.isRefreshing) {
            if (repo.repoPath.empty()) {
                repo.refreshRequested = false;
                return;
            }

            repo.refreshRequested = false;
            repo.isRefreshing = true;

            const std::string path = repo.repoPath;

            pendingStatus_ = git::git_status_async(path);
            pendingLog_    = git::git_log_async(path, 100, 0);

            // Always load diff data so it's ready when a file is selected
            pendingDiff_ = git::git_diff_async(path);

            pendingBranches_ = git::git_branch_list_async(path);
            pendingHead_     = git::git_rev_parse_head_async(path);
        }

        // Nothing in flight -- early out.
        if (!repo.isRefreshing) return;

        // ---- Phase 2: poll each future (non-blocking) ----
        using namespace std::chrono_literals;

        if (pendingStatus_ &&
            pendingStatus_->wait_for(0s) ==
                std::future_status::ready) {
            auto result = pendingStatus_->get();
            pendingStatus_.reset();
            if (result.success()) {
                auto parsed =
                    git::parse_status(result.stdout_str());
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

        if (pendingLog_ &&
            pendingLog_->wait_for(0s) ==
                std::future_status::ready) {
            auto result = pendingLog_->get();
            pendingLog_.reset();
            if (result.success()) {
                repo.commitLog =
                    git::parse_log(result.stdout_str());
                repo.commitLogLoaded =
                    static_cast<int>(repo.commitLog.size());
                repo.commitLogHasMore =
                    (repo.commitLogLoaded >= 100);
            }
        }

        if (pendingDiff_ &&
            pendingDiff_->wait_for(0s) ==
                std::future_status::ready) {
            auto result = pendingDiff_->get();
            pendingDiff_.reset();
            if (result.success()) {
                repo.currentDiff =
                    git::parse_diff(result.stdout_str());
            }
        }

        if (pendingBranches_ &&
            pendingBranches_->wait_for(0s) ==
                std::future_status::ready) {
            auto result = pendingBranches_->get();
            pendingBranches_.reset();
            if (result.success()) {
                repo.branches =
                    git::parse_branch_list(result.stdout_str());
            }
        }

        if (pendingHead_ &&
            pendingHead_->wait_for(0s) ==
                std::future_status::ready) {
            auto result = pendingHead_->get();
            pendingHead_.reset();
            if (result.success()) {
                repo.headCommitHash = result.stdout_str();
                // Trim trailing newline
                while (!repo.headCommitHash.empty() &&
                       (repo.headCommitHash.back() == '\n' ||
                        repo.headCommitHash.back() == '\r')) {
                    repo.headCommitHash.pop_back();
                }
            }
        }

        // ---- Phase 3: check if all operations completed ----
        if (!pendingStatus_ && !pendingLog_ && !pendingDiff_ &&
            !pendingBranches_ && !pendingHead_) {
            repo.isRefreshing = false;
        }
    }

private:
    // Pending futures -- one per git query type.  std::nullopt means
    // either never launched or already consumed.
    std::optional<std::future<git::GitResult>> pendingStatus_;
    std::optional<std::future<git::GitResult>> pendingLog_;
    std::optional<std::future<git::GitResult>> pendingDiff_;
    std::optional<std::future<git::GitResult>> pendingBranches_;
    std::optional<std::future<git::GitResult>> pendingHead_;
};

}  // namespace ecs
