#pragma once

#include "../../vendor/afterhours/src/core/system.h"
#include "../git/git_parser.h"
#include "../git/git_runner.h"
#include "components.h"

namespace ecs {

// GitDataRefreshSystem: Consumes refreshRequested and populates RepoComponent
// with data from git status, log, diff, and branch list.
struct GitDataRefreshSystem : afterhours::System<RepoComponent> {
    void for_each_with(afterhours::Entity& /*entity*/,
                       RepoComponent& repo, float) override {
        if (!repo.refreshRequested) return;
        if (repo.repoPath.empty()) return;

        repo.refreshRequested = false;
        repo.isRefreshing = true;

        // Refresh status
        auto statusResult = git::git_status(repo.repoPath);
        if (statusResult.success()) {
            auto parsed = git::parse_status(statusResult.stdout_str());
            repo.currentBranch = parsed.branchName;
            repo.isDetachedHead = parsed.isDetachedHead;
            repo.aheadCount = parsed.aheadCount;
            repo.behindCount = parsed.behindCount;
            repo.stagedFiles = std::move(parsed.stagedFiles);
            repo.unstagedFiles = std::move(parsed.unstagedFiles);
            repo.untrackedFiles = std::move(parsed.untrackedFiles);
            repo.isDirty = !repo.stagedFiles.empty() ||
                           !repo.unstagedFiles.empty() ||
                           !repo.untrackedFiles.empty();
        }

        // Refresh commit log
        auto logResult = git::git_log(repo.repoPath, 100, 0);
        if (logResult.success()) {
            repo.commitLog = git::parse_log(logResult.stdout_str());
            repo.commitLogLoaded = static_cast<int>(repo.commitLog.size());
            repo.commitLogHasMore = (repo.commitLogLoaded >= 100);
        }

        // Always load diff data so it's ready when a file is selected
        auto diffResult = git::git_diff(repo.repoPath);
        if (diffResult.success()) {
            repo.currentDiff = git::parse_diff(diffResult.stdout_str());
        }

        // Refresh branch list (T031)
        auto branchResult = git::git_branch_list(repo.repoPath);
        if (branchResult.success()) {
            repo.branches = git::parse_branch_list(branchResult.stdout_str());
        }

        // Get HEAD hash
        auto headResult = git::git_rev_parse_head(repo.repoPath);
        if (headResult.success()) {
            repo.headCommitHash = headResult.stdout_str();
            // Trim trailing newline
            while (!repo.headCommitHash.empty() &&
                   (repo.headCommitHash.back() == '\n' ||
                    repo.headCommitHash.back() == '\r')) {
                repo.headCommitHash.pop_back();
            }
        }

        repo.isRefreshing = false;
    }
};

}  // namespace ecs
