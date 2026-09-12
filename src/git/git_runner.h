#pragma once

#include <functional>
#include <future>
#include <optional>
#include <unordered_map>
#include <string>
#include <vector>

#include "../util/process.h"
#include "../util/async_task.h"

namespace git {

struct GitResult {
    ProcessResult raw;
    bool success() const { return raw.success(); }
    const std::string& stdout_str() const { return raw.stdout_str; }
    const std::string& stderr_str() const { return raw.stderr_str; }
    int exit_code() const { return raw.exit_code; }
};

struct RevisionComparison {
    GitResult patch;
    std::string base;
    std::string target;
};

async_work::Task<RevisionComparison> git_compare_async(const std::string& repo,
    const std::string& base, const std::string& target, bool mergeBase,
    int context, bool ignoreWhitespace);

// Log callback type -- called for every git command executed
using LogCallback = std::function<void(const std::string& command,
                                       const std::string& output,
                                       const std::string& error,
                                       bool success)>;

// Set the global log callback (called by T038 command log)
void set_log_callback(LogCallback cb);

// Synchronous git execution
// Runs: git -C <repo_path> <args...>
GitResult git_run(const std::string& repo_path,
                  const std::vector<std::string>& args, std::stop_token stop = {});

// Asynchronous git execution (for push/pull/fetch)
// Reads started before the window exists, adopted by the first refresh.
//
// Nothing about `git status` needs a window, but the refresh systems only run
// once one is up, and window+GPU init is hundreds of ms unloaded and many
// seconds on a busy machine. Kicking these off from main() hides their whole
// cost behind that wait.
struct PrefetchedReads {
    std::optional<async_work::Task<GitResult>> status;
    std::optional<async_work::Task<GitResult>> log;
    std::optional<async_work::Task<GitResult>> diff;
    std::optional<async_work::Task<GitResult>> branches;
};

// Start the startup reads for repo_path. Cheap to call for a path already
// prefetched: the second call is ignored.
void prefetch_repo(const std::string& repo_path);

// Hand over repo_path's prefetched reads, if any. They are removed, so a
// later refresh spawns fresh commands rather than replaying stale ones.
bool take_prefetched(const std::string& repo_path, PrefetchedReads& out);

async_work::Task<GitResult> git_run_async(
    const std::string& repo_path,
    const std::vector<std::string>& args);

// --- Convenience wrappers ---

// git status --porcelain=v2
GitResult git_status(const std::string& repo_path, std::stop_token stop = {});

// git log with machine-readable NUL-separated format
// max_count: number of commits to fetch (0 = unlimited)
// skip: number of commits to skip (for pagination)
GitResult git_log(const std::string& repo_path, int max_count = 100,
                  int skip = 0, std::stop_token stop = {});

// git diff (unstaged changes)
GitResult git_diff(const std::string& repo_path, std::stop_token stop = {});

// git commit -m <message>
GitResult git_commit(const std::string& repo_path,
                     const std::string& message);

// git branch --list --format (machine-readable)
GitResult git_branch_list(const std::string& repo_path, std::stop_token stop = {});

// git rev-parse HEAD (get current commit hash)
GitResult git_rev_parse_head(const std::string& repo_path, std::stop_token stop = {});

// git show <hash> --format="" (diff for a specific commit)
GitResult git_show(const std::string& repo_path,
                   const std::string& commit_hash);

// git show <hash> with full metadata (subject, body, author, date, parents)
// Format: subject\0body\0author\0authorEmail\0date\0parentHashes\0decorations
GitResult git_show_commit_info(const std::string& repo_path,
                                const std::string& commit_hash);

// --- Async convenience wrappers ---
// Each runs the corresponding git command on a background thread via
// std::async.  The returned future becomes ready when the subprocess
// completes.  Poll with wait_for(0s) from the main/UI thread to avoid
// blocking.

async_work::Task<GitResult> git_status_async(const std::string& repo_path);

async_work::Task<GitResult> git_log_async(const std::string& repo_path,
                                      int max_count = 100, int skip = 0);

async_work::Task<GitResult> git_diff_async(const std::string& repo_path);

async_work::Task<GitResult> git_branch_list_async(const std::string& repo_path);

async_work::Task<GitResult> git_rev_parse_head_async(
    const std::string& repo_path);

}  // namespace git
