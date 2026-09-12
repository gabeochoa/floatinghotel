#pragma once

#include <chrono>
#include <libproc.h>
#include <sys/time.h>
#include <unistd.h>
#include <future>
#include <optional>
#include <unordered_map>

#include "../../vendor/afterhours/src/core/system.h"
#include "../../vendor/afterhours/src/logging.h"
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
            refreshStart_[id] = std::chrono::steady_clock::now();

            const std::string path = repo.repoPath;
            auto& pf = pending_[id];
            pf.files = git::git_run_async(path, {"ls-files", "--cached", "--others", "--exclude-standard", "-z"}, async_work::Priority::Background);
            std::vector<std::string> diffArgs{"diff"};
            diffArgs.push_back("--unified=" + std::to_string(repo.diffContext));
            if (repo.ignoreWhitespace) diffArgs.push_back("--ignore-all-space");
            auto stagedArgs = diffArgs;
            stagedArgs.push_back("--cached");
            pf.stagedDiff = git::git_run_async(path, stagedArgs, async_work::Priority::Background);
            // The first refresh usually finds its commands already running:
            // main() starts them before the window exists (git::prefetch_repo).
            git::PrefetchedReads pre;
            if (git::take_prefetched(path, pre)) {
                pf.status   = std::move(pre.status);
                pf.log      = std::move(pre.log);
                pf.diff     = repo.ignoreWhitespace || repo.diffContext != 3
                                  ? git::git_run_async(path, diffArgs, async_work::Priority::Background) : std::move(pre.diff);
                pf.branches = std::move(pre.branches);
                log_info("refresh: adopted prefetched reads");
            } else {
                pf.status   = git::git_status_async(path);
                pf.log      = git::git_log_async(path, 100, 0);
                pf.diff     = git::git_run_async(path, diffArgs, async_work::Priority::Background);
                pf.branches = git::git_branch_list_async(path);
            }
            repo.commitLogLoading = true;
            // No rev-parse HEAD: the first log entry is HEAD, and every
            // subprocess is one more spawn on the startup path.
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
            log_info("refresh: status ready at {} ms", ms_since(id));
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
            // The file list is what the spinner stands in for; the log and
            // branches fill in behind it rather than holding the whole UI.
            repo.hasLoadedOnce = true;
            ++repo.dataGeneration;
        }

        if (pf.log &&
            pf.log->wait_for(0s) == std::future_status::ready) {
            auto result = pf.log->get();
            pf.log.reset();
            repo.commitLogLoading = false;
            if (result.success()) {
                repo.commitLog = git::parse_log(result.stdout_str());
                repo.commitLogLoaded =
                    static_cast<int>(repo.commitLog.size());
                repo.commitLogHasMore = (repo.commitLogLoaded >= 100);
                repo.headCommitHash =
                    repo.commitLog.empty() ? std::string() : repo.commitLog.front().hash;
            }
            log_info("commits loaded: {} commits, {} ms after refresh "
                     "requested, {} ms since process start",
                     repo.commitLog.size(), ms_since(id),
                     ms_since_process_start());
        }

        if (pf.diff &&
            pf.diff->wait_for(0s) == std::future_status::ready) {
            auto result = pf.diff->get();
            pf.diff.reset();
            log_info("refresh: diff ready at {} ms", ms_since(id));
            if (result.success()) {
                repo.currentDiff = git::parse_diff(result.stdout_str());
            }
        }

        if (pf.stagedDiff && pf.stagedDiff->wait_for(0s) == std::future_status::ready) {
            auto result = pf.stagedDiff->get();
            pf.stagedDiff.reset();
            if (result.success()) repo.stagedDiff = git::parse_diff(result.stdout_str());
        }

        if (pf.branches &&
            pf.branches->wait_for(0s) == std::future_status::ready) {
            auto result = pf.branches->get();
            pf.branches.reset();
            log_info("refresh: branches ready at {} ms", ms_since(id));
            if (result.success()) {
                repo.branches =
                    git::parse_branch_list(result.stdout_str());
            }
        }

        if (pf.files && pf.files->wait_for(0s) == std::future_status::ready) {
            auto result = pf.files->get();
            pf.files.reset();
            repo.filesError = result.success() ? "" : result.stderr_str();
            repo.allFilePaths = result.success() ? git::parse_null_paths(result.stdout_str()) : std::vector<std::string>{};
        }
        if (!pf.status && !pf.log && !pf.diff && !pf.stagedDiff && !pf.branches && !pf.files) {
            repo.isRefreshing = false;
            repo.hasLoadedOnce = true;
            pending_.erase(it);
            log_info("refresh: done in {} ms", ms_since(id));
        }
    }

private:
    // Wall time since the process was exec'd, so the number lines up with what
    // a stopwatch started at launch would read (the refresh itself only
    // starts once the window is up).
    static long ms_since_process_start() {
        struct proc_bsdinfo bi;
        struct timeval now;
        if (proc_pidinfo(getpid(), PROC_PIDTBSDINFO, 0, &bi, sizeof bi) != sizeof bi ||
            gettimeofday(&now, nullptr) != 0) {
            return -1;
        }
        return (now.tv_sec - static_cast<long>(bi.pbi_start_tvsec)) * 1000 +
               (now.tv_usec - static_cast<long>(bi.pbi_start_tvusec)) / 1000;
    }

    long ms_since(afterhours::EntityID id) const {
        auto it = refreshStart_.find(id);
        if (it == refreshStart_.end()) return -1;
        return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - it->second).count());
    }
    std::unordered_map<afterhours::EntityID, std::chrono::steady_clock::time_point> refreshStart_;

    struct PendingFutures {
        std::optional<async_work::Task<git::GitResult>> files;
        std::optional<async_work::Task<git::GitResult>> status;
        std::optional<async_work::Task<git::GitResult>> log;
        std::optional<async_work::Task<git::GitResult>> diff;
        std::optional<async_work::Task<git::GitResult>> stagedDiff;
        std::optional<async_work::Task<git::GitResult>> branches;
    };

    std::unordered_map<afterhours::EntityID, PendingFutures> pending_;
};

}  // namespace ecs
