#include "git_runner.h"

#include <mutex>
#include <thread>

namespace git {

static LogCallback g_log_callback = nullptr;
static std::mutex g_log_mutex;

void set_log_callback(LogCallback cb) { g_log_callback = cb; }

namespace {

std::string build_command_string(
    const std::vector<std::string>& cmd) {
    std::string result;
    for (size_t i = 0; i < cmd.size(); ++i) {
        if (i > 0) result += ' ';
        result += cmd[i];
    }
    return result;
}

}  // namespace

GitResult git_run(const std::string& repo_path,
                  const std::vector<std::string>& args) {
    std::vector<std::string> cmd = {"git"};
    if (!repo_path.empty()) {
        cmd.push_back("-C");
        cmd.push_back(repo_path);
    }
    cmd.insert(cmd.end(), args.begin(), args.end());

    GitResult result;
    result.raw = run_process("", cmd);

    if (g_log_callback) {
        std::lock_guard lock(g_log_mutex);
        g_log_callback(build_command_string(cmd), result.stdout_str(),
                       result.stderr_str(), result.success());
    }

    return result;
}

std::future<GitResult> git_run_async(
    const std::string& repo_path,
    const std::vector<std::string>& args) {
    std::packaged_task<GitResult()> task(
        [repo_path, args]() { return git_run(repo_path, args); });
    auto future = task.get_future();
    std::thread(std::move(task)).detach();
    return future;
}

// --- Convenience wrappers ---

GitResult git_status(const std::string& repo_path) {
    return git_run(repo_path,
                   {"status", "--porcelain=v2", "--branch"});
}

GitResult git_log(const std::string& repo_path, int max_count, int skip) {
    // Machine-readable format with NUL separators:
    // hash\0shortHash\0subject\0author\0date\0decorations\0parentHashes
    std::vector<std::string> args = {
        "log",
        "--format=%H%x00%h%x00%s%x00%an%x00%aI%x00%D%x00%P",
    };
    if (max_count > 0) {
        args.push_back("-" + std::to_string(max_count));
    }
    if (skip > 0) {
        args.push_back("--skip=" + std::to_string(skip));
    }
    return git_run(repo_path, args);
}

GitResult git_diff(const std::string& repo_path) {
    return git_run(repo_path, {"diff"});
}

GitResult git_commit(const std::string& repo_path,
                     const std::string& message) {
    return git_run(repo_path, {"commit", "-m", message});
}

GitResult git_branch_list(const std::string& repo_path) {
    // Machine-readable branch listing:
    // refname|objectname|HEAD|upstream|upstream_track
    return git_run(
        repo_path,
        {"branch", "--list",
         "--format=%(refname:short)|%(objectname:short)"
                   "|%(HEAD)|%(upstream:short)|%(upstream:track)"});
}

GitResult git_rev_parse_head(const std::string& repo_path) {
    return git_run(repo_path, {"rev-parse", "HEAD"});
}

GitResult git_show(const std::string& repo_path,
                   const std::string& commit_hash) {
    return git_run(repo_path, {"show", commit_hash, "--format="});
}

GitResult git_show_commit_info(const std::string& repo_path,
                                const std::string& commit_hash) {
    // Format: subject\0body\0author\0authorEmail\0date\0parentHashes\0decorations
    return git_run(repo_path, {
        "show", commit_hash, "--no-patch",
        "--format=%s%x00%b%x00%an%x00%ae%x00%aI%x00%P%x00%D"
    });
}

// --- Async convenience wrappers ---

std::future<GitResult> git_status_async(const std::string& repo_path) {
    return git_run_async(repo_path,
                         {"status", "--porcelain=v2", "--branch"});
}

std::future<GitResult> git_log_async(const std::string& repo_path,
                                      int max_count, int skip) {
    std::vector<std::string> args = {
        "log",
        "--format=%H%x00%h%x00%s%x00%an%x00%aI%x00%D%x00%P",
    };
    if (max_count > 0) {
        args.push_back("-" + std::to_string(max_count));
    }
    if (skip > 0) {
        args.push_back("--skip=" + std::to_string(skip));
    }
    return git_run_async(repo_path, args);
}

std::future<GitResult> git_diff_async(const std::string& repo_path) {
    return git_run_async(repo_path, {"diff"});
}

std::future<GitResult> git_branch_list_async(
    const std::string& repo_path) {
    return git_run_async(
        repo_path,
        {"branch", "--list",
         "--format=%(refname:short)|%(objectname:short)"
                   "|%(HEAD)|%(upstream:short)|%(upstream:track)"});
}

std::future<GitResult> git_rev_parse_head_async(
    const std::string& repo_path) {
    return git_run_async(repo_path, {"rev-parse", "HEAD"});
}

}  // namespace git
