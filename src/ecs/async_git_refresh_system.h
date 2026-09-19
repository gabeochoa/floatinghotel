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
#include "../git/content_reader.h"
#include "../git/git_runner.h"
#include "components.h"

namespace ecs {

struct AsyncGitDataRefreshSystem : afterhours::System<RepoComponent> {

    void once(float) override {
        for (auto it = pending_.begin(); it != pending_.end();) {
            auto tab = afterhours::EntityHelper::getEntityForID(it->first);
            if (!tab.valid() || tab->cleanup || !tab->has<RepoComponent>()) {
                refreshStart_.erase(it->first);
                it = pending_.erase(it);
            } else ++it;
        }
    }

    void for_each_with(afterhours::Entity& entity,
                       RepoComponent& repo, float) override {

        if (entity.cleanup) return;

        auto id = entity.id;

        if (!entity.has<ActiveTab>() && !repo.hasLoadedOnce && !repo.isRefreshing)
            return;

        // Phase 1: kick off async operations for any tab that requests refresh
        if (repo.refreshRequested && !repo.isRefreshing) {
            if (repo.repoPath.empty()) {
                repo.refreshRequested = false;
                return;
            }

            repo.refreshRequested = false;
            repo.isRefreshing = true;
            refreshStart_[id] = std::chrono::steady_clock::now();
            auto scope = repo.refreshScope;
            if (scope == refresh_scope::Scope::None) scope = refresh_scope::Scope::Full;
            repo.refreshScope = refresh_scope::Scope::None;
            auto plan = refresh_scope::plan(scope);
            if (plan.log) repo.commitLogPage = {};
            repo.lastRefreshScope = refresh_scope::name(scope);

            const std::string path = repo.repoPath;
            auto& pf = pending_[id];
            pf.repository = path;
            pf.historyKey = repo.historyScope.key();
            std::vector<std::string> diffArgs{"diff"};
            diffArgs.push_back("--unified=" + std::to_string(repo.diffContext));
            if (repo.ignoreWhitespace) diffArgs.push_back("--ignore-all-space");
            auto stagedArgs = diffArgs;
            stagedArgs.push_back("--cached");
            if (plan.files)
                pf.files = git::git_run_async(path, {"ls-files", "--cached", "--others", "--exclude-standard", "-z"}, async_work::Priority::Background);
            if (scope == refresh_scope::Scope::Full) {
                git::PrefetchedReads pre;
                if (git::take_prefetched(path, pre)) {
                    pf.status   = std::move(pre.status);
                    pf.log      = repo.historyScope == git::HistoryScope{} ? std::move(pre.log) : git::git_run_async(path, git::history_scope_args(repo.historyScope));
                    pf.diff     = repo.ignoreWhitespace || repo.diffContext != 3
                                      ? git::git_run_async(path, diffArgs, async_work::Priority::Background) : std::move(pre.diff);
                    pf.branches = std::move(pre.branches);
                    log_info("refresh: adopted prefetched reads");
                } else {
                    if (plan.status) pf.status = git::git_status_async(path);
                    if (plan.log) pf.log = git::git_run_async(path, git::history_scope_args(repo.historyScope));
                    if (plan.diff) pf.diff = git::git_run_async(path, diffArgs, async_work::Priority::Background);
                    if (plan.branches) pf.branches = git::git_branch_list_async(path);
                }
            } else {
                if (plan.status) pf.status = git::git_status_async(path);
                if (plan.log) pf.log = git::git_run_async(path, git::history_scope_args(repo.historyScope));
                if (plan.diff) pf.diff = git::git_run_async(path, diffArgs, async_work::Priority::Background);
                if (plan.branches) pf.branches = git::git_branch_list_async(path);
            }
            if (plan.stagedDiff) pf.stagedDiff = git::git_run_async(path, stagedArgs, async_work::Priority::Background);
            repo.commitLogLoading = plan.log;
            // No rev-parse HEAD: the first log entry is HEAD, and every
            // subprocess is one more spawn on the startup path.
        }

        update_history_scope(repo);
        update_history_page(repo);
        if (!repo.isRefreshing) {
            if (repo.untrackedReviewFuture.valid() &&
                (repo.untrackedReviewGeneration != repo.dataGeneration || repo.untrackedReviewRepository != repo.repoPath))
                repo.untrackedReviewFuture = {};
            if (repo.untrackedReviewFuture.valid() && repo.untrackedReviewFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                auto result = repo.untrackedReviewFuture.get();
                repo.untrackedReviewNotice = std::move(result.notice);
                for (auto& file : result.files) {
                    if (std::none_of(repo.currentDiff.begin(), repo.currentDiff.end(), [&](const auto& existing) {
                        return existing.filePath == file.filePath;
                    })) repo.currentDiff.push_back(std::move(file));
                }
                ++repo.patchGeneration;
            }
            if (repo.hasLoadedOnce && (repo.untrackedReviewGeneration != repo.dataGeneration ||
                                      repo.untrackedReviewRepository != repo.repoPath)) {
                repo.untrackedReviewGeneration = repo.dataGeneration;
                repo.untrackedReviewRepository = repo.repoPath;
                repo.untrackedReviewNotice.clear();
                if (!repo.untrackedFiles.empty()) {
                    repo.untrackedReviewLoading.restart();
                    repo.untrackedReviewFuture = git::read_untracked_review_files_async(repo.repoPath, repo.untrackedFiles);
                }
            }
            return;
        }

        auto it = pending_.find(id);
        if (it == pending_.end()) {
            repo.isRefreshing = false;
            return;
        }
        auto& pf = it->second;
        if (pf.repository != repo.repoPath) {
            pending_.erase(it);
            repo.isRefreshing = false;
            repo.statusKnown = repo.branchKnown = false;
            repo.refreshRequested = true;
            return;
        }

        // Phase 2: poll each future (non-blocking)
        using namespace std::chrono_literals;

        if (pf.status &&
            pf.status->wait_for(0s) == std::future_status::ready) {
            auto result = pf.status->get();
            pf.status.reset();
            log_info("refresh: status ready at {} ms", ms_since(id));
            if (result.success()) {
                auto parsed = git::parse_status(result.stdout_str());
                repo.headCommitHash = parsed.headHash;
                repo.statusKnown = repo.branchKnown = true;
                repo.statusError.clear();
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
            if (!result.success()) { repo.statusKnown = false; repo.statusError = result.stderr_str().empty() ? "Unable to read working changes" : result.stderr_str(); }
            // The file list is what the spinner stands in for; the log and
            // branches fill in behind it rather than holding the whole UI.
            repo.hasLoadedOnce = true;
            ++repo.dataGeneration;
        }

        if (pf.log &&
            pf.log->wait_for(0s) == std::future_status::ready) {
            auto result = pf.log->get();
            pf.log.reset();
            if (!repo.historyScopeFuture.valid()) repo.commitLogLoading = false;
            if (result.success() && pf.historyKey == repo.historyScope.key() && !repo.historyScopeFuture.valid()) {
                repo.historyError.clear();
                repo.commitLog = git::parse_log(result.stdout_str());
                repo.commitLogLoaded =
                    static_cast<int>(repo.commitLog.size());
                repo.commitLogHasMore = (repo.commitLogLoaded >= 100);
                if (repo.historyScope.mode == git::HistoryScope::Mode::Current && !repo.historyScope.remotes)
                    repo.headCommitHash = repo.commitLog.empty() ? std::string() : repo.commitLog.front().hash;
            }
            if (!result.success() && pf.historyKey == repo.historyScope.key()) repo.historyError = result.stderr_str();
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
                ++repo.patchGeneration;
            }
        }

        if (pf.stagedDiff && pf.stagedDiff->wait_for(0s) == std::future_status::ready) {
            auto result = pf.stagedDiff->get();
            pf.stagedDiff.reset();
            if (result.success()) {
                repo.stagedDiff = git::parse_diff(result.stdout_str());
                ++repo.patchGeneration;
            }
        }

        if (pf.branches &&
            pf.branches->wait_for(0s) == std::future_status::ready) {
            auto result = pf.branches->get();
            pf.branches.reset();
            log_info("refresh: branches ready at {} ms", ms_since(id));
            if (result.success()) {
                repo.branches =
                    git::parse_branch_list(result.stdout_str());
                for (const auto& branch : repo.branches) if (branch.isCurrent) {
                    repo.currentBranch = branch.name;
                    repo.branchKnown = true;
                }
            }
        }

        if (pf.files && pf.files->wait_for(0s) == std::future_status::ready) {
            auto result = pf.files->get();
            pf.files.reset();
            repo.filesError = result.success() ? "" : result.stderr_str();
            repo.allFilePaths = result.success() ? git::parse_null_paths(result.stdout_str()) : std::vector<std::string>{};
            ++repo.allFilePathsGeneration;
        }
        if (!pf.status && !pf.log && !pf.diff && !pf.stagedDiff && !pf.branches && !pf.files) {
            repo.isRefreshing = false;
            repo.hasLoadedOnce = true;
            pending_.erase(it);
            log_info("refresh: done in {} ms", ms_since(id));
            refreshStart_.erase(id);
        }
    }

private:
    static void update_history_scope(RepoComponent& repo) {
        const auto key = repo.repoPath + ":" + repo.historyScope.key();
        if (repo.historyScopeRequested) {
            repo.historyScopeRequested = false;
            repo.historyScopeRequestKey = key;
            repo.historyScopeFuture = git::git_run_async(repo.repoPath, git::history_scope_args(repo.historyScope));
            repo.commitLogPage = {};
            repo.commitLog.clear();
            repo.commitLogLoading = true;
            repo.historyError.clear();
        }
        if (repo.historyScopeFuture.valid() && repo.historyScopeFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto result = repo.historyScopeFuture.get();
            if (key != repo.historyScopeRequestKey) return;
            repo.commitLogLoading = false;
            if (!result.success()) { repo.historyError = result.stderr_str(); return; }
            repo.commitLog = git::parse_log(result.stdout_str());
            repo.commitLogLoaded = static_cast<int>(repo.commitLog.size());
            repo.commitLogHasMore = repo.commitLog.size() == 100;
            repo.historyRestorePosition = true;
        }
    }

    static void update_history_page(RepoComponent& repo) {
        auto& page = repo.commitLogPage;
        if (page.future.valid() && (page.repository != repo.repoPath || page.head != repo.headCommitHash ||
                page.offset != repo.commitLog.size())) page = {};
        if (page.future.valid() && page.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto result = page.future.get();
            if (result.success()) {
                auto commits = git::parse_log(result.stdout_str());
                repo.commitLogHasMore = commits.size() == 100;
                repo.commitLog.insert(repo.commitLog.end(), std::make_move_iterator(commits.begin()), std::make_move_iterator(commits.end()));
                repo.commitLogLoaded = static_cast<int>(repo.commitLog.size());
                page.error.clear();
            } else page.error = result.stderr_str().empty() ? "Unable to load older commits" : result.stderr_str();
        }
        if (!page.requested || page.future.valid() || repo.isRefreshing || repo.refreshRequested) return;
        page.requested = false;
        if (!repo.commitLogHasMore || repo.commitLog.empty()) return;
        page.repository = repo.repoPath;
        page.head = repo.headCommitHash;
        page.offset = repo.commitLog.size();
        page.error.clear();
        auto args = git::history_scope_args(repo.historyScope, page.offset);
        if (repo.historyScope.mode == git::HistoryScope::Mode::Current && !repo.historyScope.remotes)
            for (auto& arg : args) if (arg == "HEAD" && !page.head.empty()) arg = page.head;
        page.future = git::git_run_async(page.repository, args, async_work::Priority::Background);
    }

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
        std::string repository;
        std::string historyKey;
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
