#pragma once

#include <chrono>
#include <libproc.h>
#include <sys/time.h>
#include <unistd.h>
#include <future>
#include <optional>
#include <unordered_map>

#include <afterhours/src/core/system.h>
#include <afterhours/src/logging.h>
#include "../util/signpost.h"
#include "../git/git_parser.h"
#include "../git/commit_patch.h"
#include "../git/content_reader.h"
#include "../git/git_runner.h"
#include "../util/diff_reconcile.h"
#include "../util/navigation.h"
#include "components.h"
#include "../util/commit_message.h"
#include <afterhours/src/plugins/files.h>

namespace ecs {

// Refresh reads whose git output is parsed on the worker, so the UI
// thread only reconciles finished structures on the switch path.
struct ParsedStatus { git::GitResult raw; git::StatusResult parsed; };
struct ParsedLog { git::GitResult raw; std::vector<CommitEntry> parsed; };
struct ParsedDiff { git::GitResult raw; std::vector<FileDiff> parsed; };
struct ParsedBranches { git::GitResult raw; std::vector<BranchInfo> parsed; };

template <typename Out, typename Parse>
async_work::Task<Out> parsed_async(async_work::Task<git::GitResult> task, Parse parse) {
    // The wait for the git task must not occupy a pool worker: with a small
    // executor (FH_EXECUTOR_THREADS=2) five of these Foreground waiters
    // starved the Background git jobs they were waiting on and the app
    // deadlocked at startup. Wait + parse on a dedicated transient thread.
    auto shared = std::make_shared<async_work::Task<git::GitResult>>(std::move(task));
    // Promise-based on purpose: a std::async future blocks in its
    // destructor until the job finishes, so dropping a parsed task whose
    // git read waits on the repository lock (tab closed mid-commit) would
    // deadlock the destroying thread. A detached thread feeding a promise
    // keeps destruction non-blocking, like launch_on's futures.
    auto promise = std::make_shared<std::promise<Out>>();
    auto future = promise->get_future();
    try {
        std::thread([shared, parse, promise]() mutable {
            try {
                auto raw = shared->get();
                Out out;
                out.raw = raw;
                if (raw.success()) out.parsed = parse(raw.stdout_str());
                promise->set_value(std::move(out));
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        }).detach();
    } catch (...) {
        return async_work::completed(
            Out{git::GitResult{{"", "Background queue is full; retry the action", -1}}, {}});
    }
    return async_work::Task<Out>(std::move(future), std::stop_source{std::nostopstate});
}

struct AsyncGitDataRefreshSystem : afterhours::System<RepoComponent> {

    // The repository's commit.template, read once per tab. It used to run
    // synchronously on the UI thread when the sidebar first drew (hundreds
    // of ms per git spawn). Loaded here so it follows the refresh and is
    // consumed even while the commit box is not on screen; wait_for_refresh
    // waits for it.
    static void load_commit_template(afterhours::Entity& entity, const RepoComponent& repo) {
        if (repo.repoPath.empty() || !entity.has<CommitEditorComponent>()) return;
        auto& editor = entity.get<CommitEditorComponent>();
        if (editor.templateLoaded) return;
        if (!editor.templateTask.valid()) {
            editor.templateTask = git::git_run_async(repo.repoPath, {"config", "--get", "--path", "commit.template"},
                                                     async_work::Priority::Background);
            return;
        }
        if (editor.templateTask.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        editor.templateLoaded = true;
        auto path = editor.templateTask.get();
        if (!path.success()) return;
        std::filesystem::path file = commit_message::strip_template(path.stdout_str());
        if (file.is_relative()) file = std::filesystem::path(repo.repoPath) / file;
        editor.templateText = commit_message::strip_template(afterhours::files::read_string(file).value_or(""));
        if (editor.subject.empty()) {
            editor.subject = editor.templateText;
            editor.placeCaret = 0;
        }
    }


    void once(float) override {
        if (headPrefetch_.valid() && headPrefetch_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try { (void)headPrefetch_.get(); } catch (...) {}
            headPrefetch_ = {};
        }
        if (warming_) {
            auto tab = afterhours::EntityHelper::getEntityForID(warmingId_);
            if (!tab.valid() || tab->cleanup || !tab->has<RepoComponent>()) warming_ = false;
        }
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
        static auto signpostLog = signpost::log_for("refresh");
        signpost::Interval signpostInterval(signpostLog, "refresh_tick", repo.repoPath);
        if (entity.cleanup) return;

        auto id = entity.id;

        if (entity.has<ActiveTab>() && repo.has_loaded_once()) sawActiveLoaded_ = true;
        if (!entity.has<ActiveTab>() && !repo.has_loaded_once() && !repo.refreshing()) {
            // Warm one restored inactive tab at a time once the active tab
            // has loaded, so first activation is not a cold refresh.
            if (!sawActiveLoaded_ || warming_ || repo.repoPath.empty()) return;
            warming_ = true;
            warmingId_ = id;
            repo.request_refresh();
            log_info("refresh: warming restored tab {}", repo.repoPath);
        }

        // Phase 1: kick off async operations for any tab that requests refresh
        if (repo.refresh_requested()) {
            if (repo.repoPath.empty()) {
                repo.abandon_refresh();
                return;
            }

            refreshStart_[id] = std::chrono::steady_clock::now();
            auto scope = repo.begin_refresh();
            auto plan = refresh_scope::plan(scope);
            if (plan.log) repo.commitLogPage = {};
            repo.lastRefreshScope = refresh_scope::name(scope);

            const std::string path = repo.repoPath;
            auto& pf = pending_[id];
            pf.repository = path;
            pf.historyKey = repo.historyScope.key();
            pf.headBefore = repo.headCommitHash;
            pf.selectedBefore = repo.selectedCommitHash();
            pf.scope = scope;
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
                    pf.status   = parsed_async<ParsedStatus>(std::move(pre.status).value(), [](const std::string& out) { return git::parse_status(out); });
                    pf.log      = parsed_async<ParsedLog>(repo.historyScope == git::HistoryScope{} ? std::move(pre.log).value() : git::git_run_async(path, git::history_scope_args(repo.historyScope)),
                                      [](const std::string& out) { return git::parse_log(out); });
                    pf.diff     = parsed_async<ParsedDiff>(repo.ignoreWhitespace || repo.diffContext != 3
                                      ? git::git_run_async(path, diffArgs, async_work::Priority::Background) : std::move(pre.diff).value(),
                                      [](const std::string& out) { return git::parse_diff(out); });
                    pf.branches = parsed_async<ParsedBranches>(std::move(pre.branches).value(), [](const std::string& out) { return git::parse_branch_list(out); });
                    log_info("refresh: adopted prefetched reads");
                } else {
                    if (plan.status) pf.status = parsed_async<ParsedStatus>(git::git_status_async(path), [](const std::string& out) { return git::parse_status(out); });
                    if (plan.log) pf.log = parsed_async<ParsedLog>(git::git_run_async(path, git::history_scope_args(repo.historyScope)), [](const std::string& out) { return git::parse_log(out); });
                    if (plan.diff) pf.diff = parsed_async<ParsedDiff>(git::git_run_async(path, diffArgs, async_work::Priority::Background), [](const std::string& out) { return git::parse_diff(out); });
                    if (plan.branches) pf.branches = parsed_async<ParsedBranches>(git::git_branch_list_async(path), [](const std::string& out) { return git::parse_branch_list(out); });
                }
            } else {
                if (plan.status) pf.status = parsed_async<ParsedStatus>(git::git_status_async(path), [](const std::string& out) { return git::parse_status(out); });
                if (plan.log) pf.log = parsed_async<ParsedLog>(git::git_run_async(path, git::history_scope_args(repo.historyScope)), [](const std::string& out) { return git::parse_log(out); });
                if (plan.diff) pf.diff = parsed_async<ParsedDiff>(git::git_run_async(path, diffArgs, async_work::Priority::Background), [](const std::string& out) { return git::parse_diff(out); });
                if (plan.branches) pf.branches = parsed_async<ParsedBranches>(git::git_branch_list_async(path), [](const std::string& out) { return git::parse_branch_list(out); });
            }
            if (plan.branches && scope == refresh_scope::Scope::Refs) {
                // Tags ride the branches for-each-ref spawn (parse_tag_list
                // splits them out of the same output in the poll below).
                // The stash list only feeds the Refs pane, so only a Refs
                // refresh spawns for it; the pane requests one when stale.
                pf.stashes = git::git_run_async(path, {"stash", "list"}, async_work::Priority::Background);
            }
            if (plan.stagedDiff) pf.stagedDiff = parsed_async<ParsedDiff>(git::git_run_async(path, stagedArgs, async_work::Priority::Background), [](const std::string& out) { return git::parse_diff(out); });
            if (plan.log) repo.commitLogPage.begin_scope();
            // No rev-parse HEAD: the first log entry is HEAD, and every
            // subprocess is one more spawn on the startup path.
        }

        update_history_scope(repo);
        update_history_page(repo);
        load_commit_template(entity, repo);
        if (!repo.refreshing()) {
            if (repo.untrackedReview.stale_in_flight(repo.dataGeneration, repo.repoPath))
                repo.untrackedReview.invalidate();
            if (auto result = repo.untrackedReview.poll()) {
                bool appended = false;
                for (auto& file : result->files) {
                    auto existing = std::find_if(repo.currentDiff.begin(), repo.currentDiff.end(), [&](const auto& value) {
                        return value.filePath == file.filePath;
                    });
                    if (existing == repo.currentDiff.end()) { repo.currentDiff.push_back(std::move(file)); appended = true; }
                    else if (!diff_reconcile::file_equal(*existing, file)) { *existing = std::move(file); appended = true; }
                }
                if (appended) ++repo.patchGeneration;
            }
            if (repo.has_loaded_once() && repo.untrackedReview.needs_read(repo.dataGeneration, repo.repoPath)) {
                if (!repo.untrackedFiles.empty()) {
                    repo.untrackedReviewLoading.restart();
                    repo.untrackedReview.begin(repo.dataGeneration, repo.repoPath,
                        git::read_untracked_review_files_async(repo.repoPath, repo.untrackedFiles));
                } else {
                    repo.untrackedReview.settle(repo.dataGeneration, repo.repoPath);
                }
            }
            return;
        }

        auto it = pending_.find(id);
        if (it == pending_.end()) {
            repo.abandon_refresh();
            return;
        }
        auto& pf = it->second;
        if (pf.repository != repo.repoPath) {
            pending_.erase(it);
            repo.requeue_refresh();
            repo.statusKnown = repo.branchKnown = false;
            return;
        }

        // Phase 2: poll each future (non-blocking)
        using namespace std::chrono_literals;

        if (pf.status &&
            pf.status->wait_for(0s) == std::future_status::ready) {
            auto outcome = pf.status->get();
            pf.status.reset();
            const auto& result = outcome.raw;
            log_info("refresh: status ready at {} ms", ms_since(id));
            bool statusChanged = !repo.has_loaded_once();
            if (result.success()) {
                auto parsed = std::move(outcome.parsed);
                statusChanged = statusChanged || repo.headCommitHash != parsed.headHash ||
                    repo.currentBranch != parsed.branchName || repo.isDetachedHead != parsed.isDetachedHead ||
                    repo.aheadCount != parsed.aheadCount || repo.behindCount != parsed.behindCount ||
                    !diff_reconcile::status_files_equal(repo.stagedFiles, parsed.stagedFiles) ||
                    !diff_reconcile::status_files_equal(repo.unstagedFiles, parsed.unstagedFiles) ||
                    repo.untrackedFiles != parsed.untrackedFiles;
                if (statusChanged) {
                    repo.headCommitHash = parsed.headHash;
                    repo.currentBranch  = parsed.branchName;
                    repo.isDetachedHead = parsed.isDetachedHead;
                    repo.aheadCount     = parsed.aheadCount;
                    repo.behindCount    = parsed.behindCount;
                    repo.stagedFiles    = std::move(parsed.stagedFiles);
                    repo.unstagedFiles  = std::move(parsed.unstagedFiles);
                    repo.untrackedFiles = std::move(parsed.untrackedFiles);
                }
                repo.statusKnown = repo.branchKnown = true;
                repo.statusError.clear();
                repo.isDirty = !repo.stagedFiles.empty() ||
                               !repo.unstagedFiles.empty() ||
                               !repo.untrackedFiles.empty();
                {
                    std::vector<std::string> conflicts;
                    auto collect = [&](const std::vector<FileStatus>& files) {
                        for (const auto& file : files)
                            if (file.indexStatus == 'U' || file.workTreeStatus == 'U' ||
                                (file.indexStatus == 'A' && file.workTreeStatus == 'A') ||
                                (file.indexStatus == 'D' && file.workTreeStatus == 'D'))
                                if (std::find(conflicts.begin(), conflicts.end(), file.path) == conflicts.end())
                                    conflicts.push_back(file.path);
                    };
                    collect(repo.stagedFiles);
                    collect(repo.unstagedFiles);
                    if (repo.conflictedFiles != conflicts) {
                        repo.conflictedFiles = std::move(conflicts);
                        ++repo.dataGeneration;
                    }
                    std::error_code ec;
                    std::filesystem::path gitDir = std::filesystem::path(repo.repoPath) / ".git";
                    if (std::filesystem::is_regular_file(gitDir, ec)) {
                        std::ifstream in(gitDir);
                        std::string line;
                        std::getline(in, line);
                        if (line.starts_with("gitdir: ")) gitDir = line.substr(8);
                    }
                    repo.mergeInProgress = std::filesystem::exists(gitDir / "MERGE_HEAD", ec);
                }
            }
            if (!result.success()) { repo.statusKnown = false; repo.statusError = result.stderr_str().empty() ? "Unable to read working changes" : result.stderr_str(); }
            repo.note_refresh_loaded();
            if (statusChanged) ++repo.dataGeneration;
        }

        if (pf.log &&
            pf.log->wait_for(0s) == std::future_status::ready) {
            auto outcome = pf.log->get();
            pf.log.reset();
            const auto& result = outcome.raw;
            if (!repo.historyScopeFuture.valid() && repo.commitLogPage.phase == LogPhase::LoadingScope)
                repo.commitLogPage.phase = LogPhase::Idle;
            if (result.success() && pf.historyKey == repo.historyScope.key() && !repo.historyScopeFuture.valid()) {
                repo.historyError.clear();
                repo.commitLogPage.requestedPageSize = git::kCommitLogPageSize;
                auto parsedLog = std::move(outcome.parsed);
                const size_t returned = parsedLog.size();
                if (!diff_reconcile::commit_log_equal(repo.commitLog, parsedLog)) {
                    repo.commitLog = std::move(parsedLog);
                    // Amended/rebased tip: a view pinned to the old HEAD hash
                    // would keep showing the dangling pre-amend commit forever.
                    if (repo.historyScope.mode == git::HistoryScope::Mode::Current && !repo.historyScope.remotes &&
                        !pf.headBefore.empty() && pf.selectedBefore == pf.headBefore &&
                        repo.selectedCommitHash() == pf.headBefore && !repo.commitLog.empty() &&
                        repo.commitLog.front().hash != pf.headBefore)
                        navigation::follow_rewritten_tip(repo, pf.headBefore, repo.commitLog.front().hash);
                }
                repo.commitLogPage.finish(returned);
                if (repo.historyScope.mode == git::HistoryScope::Mode::Current && !repo.historyScope.remotes)
                    repo.headCommitHash = repo.commitLog.empty() ? std::string() : repo.commitLog.front().hash;
                // A commit that just landed on HEAD (an agent committing
                // while the app is open) is the one the user clicks next;
                // warm its patch now so the click is a cache hit instead
                // of a cold multi-spawn load racing the refresh tail.
                if (result.success() && repo.historyScope.mode == git::HistoryScope::Mode::Current &&
                    !pf.headBefore.empty() && !repo.commitLog.empty() && repo.commitLog.front().hash != pf.headBefore &&
                    !headPrefetch_.valid() &&
                    !git::peek_commit_patch({repo.repoPath, repo.commitLog.front().hash, {}, repo.diffContext, repo.ignoreWhitespace})) {
                    if (auto task = git::prefetch_commit_patch_async(
                            {repo.repoPath, repo.commitLog.front().hash, {}, repo.diffContext, repo.ignoreWhitespace}))
                        headPrefetch_ = std::move(*task);
                }
            }
            if (!result.success() && pf.historyKey == repo.historyScope.key()) {
                repo.historyError = result.stderr_str();
                if (!repo.historyScopeFuture.valid()) repo.commitLogPage.fail(repo.historyError);
            }
            log_info("commits loaded: {} commits, {} ms after refresh "
                     "requested, {} ms since process start",
                     repo.commitLog.size(), ms_since(id),
                     ms_since_process_start());
        }

        if (pf.diff &&
            pf.diff->wait_for(0s) == std::future_status::ready) {
            auto outcome = pf.diff->get();
            pf.diff.reset();
            const auto& result = outcome.raw;
            log_info("refresh: diff ready at {} ms", ms_since(id));
            if (result.success()) {
                repo.currentDiffGeneration = repo.dataGeneration;
                if (diff_reconcile::reconcile(repo.currentDiff, std::move(outcome.parsed)))
                    ++repo.patchGeneration;
                // Untracked previews are appended to currentDiff, not in git diff:
                // reconcile just dropped them, so re-read them even if status is unchanged.
                if (!repo.untrackedFiles.empty())
                    repo.untrackedReview.invalidate();
            }
        }

        if (pf.stagedDiff && pf.stagedDiff->wait_for(0s) == std::future_status::ready) {
            auto outcome = pf.stagedDiff->get();
            pf.stagedDiff.reset();
            const auto& result = outcome.raw;
            if (result.success()) {
                repo.stagedDiffGeneration = repo.dataGeneration;
                if (diff_reconcile::reconcile(repo.stagedDiff, std::move(outcome.parsed)))
                    ++repo.patchGeneration;
            }
        }

        if (pf.branches &&
            pf.branches->wait_for(0s) == std::future_status::ready) {
            auto outcome = pf.branches->get();
            pf.branches.reset();
            const auto& result = outcome.raw;
            log_info("refresh: branches ready at {} ms", ms_since(id));
            if (result.success()) {
                auto parsedBranches = std::move(outcome.parsed);
                if (!diff_reconcile::branches_equal(repo.branches, parsedBranches)) repo.branches = std::move(parsedBranches);
                auto parsedTags = git::parse_tag_list(result.stdout_str());
                if (repo.tags != parsedTags) repo.tags = std::move(parsedTags);
                for (const auto& branch : repo.branches) if (branch.isCurrent) {
                    repo.currentBranch = branch.name;
                    repo.branchKnown = true;
                }
            }
        }

        auto pollLines = [&](auto& future, std::vector<std::string>& target) {
            if (!future || future->wait_for(0s) != std::future_status::ready) return;
            auto result = future->get();
            future.reset();
            if (!result.success()) return;
            std::vector<std::string> lines;
            std::istringstream stream(result.stdout_str());
            for (std::string line; std::getline(stream, line);)
                if (!line.empty()) lines.push_back(std::move(line));
            if (target != lines) target = std::move(lines);
        };
        if (pf.stashes) {
            pollLines(pf.stashes, repo.stashes);
            if (!pf.stashes) {
                repo.stashesGeneration = repo.dataGeneration;
                repo.stashesRequested = false;
            }
        }

        if (pf.files && pf.files->wait_for(0s) == std::future_status::ready) {
            auto result = pf.files->get();
            pf.files.reset();
            repo.filesError = result.success() ? "" : result.stderr_str();
            auto parsedPaths = result.success() ? git::parse_null_paths(result.stdout_str()) : std::vector<std::string>{};
            if (repo.allFilePaths != parsedPaths) {
                repo.allFilePaths = std::move(parsedPaths);
                ++repo.allFilePathsGeneration;
            }
        }
        if (pf.done()) {
            repo.finish_refresh();
            // A warmed (never-activated) tab's deactivatedAt is the epoch,
            // so switching to it looked like returning after an eternity
            // and re-ran git despite the warm data. Stamp the warm time:
            // the away-threshold in switch_to_tab measures from here.
            if (entity.has<Tab>()) entity.get<Tab>().deactivatedAt = std::chrono::steady_clock::now();
            if (warming_ && warmingId_ == id) warming_ = false;
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
            repo.historyScopeFuture = parsed_async<ParsedCommitLog>(
                git::git_run_async(repo.repoPath, git::history_scope_args(repo.historyScope)),
                [](const std::string& out) { return git::parse_log(out); });
            repo.commitLogPage = {};
            repo.commitLog.clear();
            repo.commitLogPage.begin_scope();
            repo.historyError.clear();
        }
        if (repo.historyScopeFuture.valid() && repo.historyScopeFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto outcome = repo.historyScopeFuture.get();
            if (key != repo.historyScopeRequestKey) return;
            if (!outcome.raw.success()) {
                repo.historyError = outcome.raw.stderr_str();
                repo.commitLogPage.fail(repo.historyError);
                return;
            }
            const size_t returned = outcome.parsed.size();
            repo.commitLogPage.requestedPageSize = git::kCommitLogPageSize;
            repo.commitLog = std::move(outcome.parsed);
            repo.commitLogPage.finish(returned);
            repo.historyRestorePosition = true;
        }
    }

    static void update_history_page(RepoComponent& repo) {
        auto& page = repo.commitLogPage;
        if (page.future.valid() && (page.repository != repo.repoPath || page.head != repo.headCommitHash ||
                page.offset != repo.commitLog.size())) page = {};
        if (page.future.valid() && page.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto outcome = page.future.get();
            page.future = {};
            if (outcome.raw.success()) {
                auto commits = std::move(outcome.parsed);
                const size_t returned = commits.size();
                repo.commitLog.insert(repo.commitLog.end(), std::make_move_iterator(commits.begin()), std::make_move_iterator(commits.end()));
                page.finish(returned);
            } else page.fail(outcome.raw.stderr_str().empty() ? "Unable to load older commits" : outcome.raw.stderr_str());
        }
        if (page.phase != LogPhase::LoadingPage || page.future.valid() || repo.refresh_busy()) return;
        if (!page.hasMore || repo.commitLog.empty()) { page.phase = LogPhase::Idle; return; }
        page.repository = repo.repoPath;
        page.head = repo.headCommitHash;
        page.offset = repo.commitLog.size();
        page.requestedPageSize = git::kCommitLogPageSize;
        page.error.clear();
        auto args = git::history_scope_args(repo.historyScope, page.offset);
        if (repo.historyScope.mode == git::HistoryScope::Mode::Current && !repo.historyScope.remotes)
            for (auto& arg : args) if (arg == "HEAD" && !page.head.empty()) arg = page.head;
        page.future = parsed_async<ParsedCommitLog>(
            git::git_run_async(page.repository, args, async_work::Priority::Background),
            [](const std::string& out) { return git::parse_log(out); });
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
        std::string headBefore;
        std::string selectedBefore;
        refresh_scope::Scope scope = refresh_scope::Scope::Full;
        std::optional<async_work::Task<git::GitResult>> files;
        std::optional<async_work::Task<ParsedStatus>> status;
        std::optional<async_work::Task<ParsedLog>> log;
        std::optional<async_work::Task<ParsedDiff>> diff;
        std::optional<async_work::Task<ParsedDiff>> stagedDiff;
        std::optional<async_work::Task<ParsedBranches>> branches;
        std::optional<async_work::Task<git::GitResult>> stashes;
        // Refresh completion (M1): every per-read future consumed. The
        // refresh poll drives Running -> Settled from this alone.
        bool done() const {
            return !status && !log && !diff && !stagedDiff && !branches && !files && !stashes;
        }
    };

    std::unordered_map<afterhours::EntityID, PendingFutures> pending_;
    async_work::Task<bool> headPrefetch_;
    bool sawActiveLoaded_ = false;
    bool warming_ = false;
    afterhours::EntityID warmingId_ = 0;
};

}  // namespace ecs
