#pragma once

#include <functional>
#include <future>
#include <string>
#include <vector>

#include "../util/process.h"

namespace git {

struct GitResult {
    ProcessResult raw;
    bool success() const { return raw.success(); }
    const std::string& stdout_str() const { return raw.stdout_str; }
    const std::string& stderr_str() const { return raw.stderr_str; }
    int exit_code() const { return raw.exit_code; }
};

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
                  const std::vector<std::string>& args);

// Asynchronous git execution (for push/pull/fetch)
std::future<GitResult> git_run_async(
    const std::string& repo_path,
    const std::vector<std::string>& args);

// --- Convenience wrappers ---

// git status --porcelain=v2
GitResult git_status(const std::string& repo_path);

// git log with machine-readable NUL-separated format
// max_count: number of commits to fetch (0 = unlimited)
// skip: number of commits to skip (for pagination)
GitResult git_log(const std::string& repo_path, int max_count = 100,
                  int skip = 0);

// git diff (unstaged changes)
GitResult git_diff(const std::string& repo_path);

// git commit -m <message>
GitResult git_commit(const std::string& repo_path,
                     const std::string& message);

// git branch --list --format (machine-readable)
GitResult git_branch_list(const std::string& repo_path);

// git rev-parse HEAD (get current commit hash)
GitResult git_rev_parse_head(const std::string& repo_path);

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

std::future<GitResult> git_status_async(const std::string& repo_path);

std::future<GitResult> git_log_async(const std::string& repo_path,
                                      int max_count = 100, int skip = 0);

std::future<GitResult> git_diff_async(const std::string& repo_path);

std::future<GitResult> git_branch_list_async(const std::string& repo_path);

std::future<GitResult> git_rev_parse_head_async(
    const std::string& repo_path);

}  // namespace git
