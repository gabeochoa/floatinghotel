#include "git_runner.h"

#include "../../vendor/afterhours/src/logging.h"

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <shared_mutex>
#include <thread>

namespace git {

static LogCallback g_log_callback = nullptr;
static std::mutex g_log_mutex;

// Serializes git writes against the repo. Reads run async on detached threads
// (git_run_async, the refresh systems) while writes like `git apply --cached`
// (Approve) run on the main thread; without this they race on
// .git/index.lock and the write fails ("Unable to create index.lock").
// Reads take the lock shared so the five startup commands overlap: on a
// loaded machine each spawn costs seconds, and running them back to back put
// the commit log 7 s behind the window and the last command 17 s behind.
static std::shared_mutex g_git_mutex;

// Commands that never touch the index or refs, so they may overlap each other
// and only need to be kept apart from writes.
static bool is_read_only(const std::vector<std::string>& args) {
    if (args.empty()) return false;
    const std::string& verb = args[0];
    if (verb == "status" || verb == "log" || verb == "diff" || verb == "grep" ||
        verb == "rev-parse" || verb == "show" || verb == "for-each-ref" ||
        verb == "ls-files" || verb == "cat-file" || verb == "rev-list" ||
        verb == "remote" || verb == "ls-remote")
        return true;
    if (verb == "branch") {
        for (const auto& a : args)
            if (a == "-d" || a == "-D" || a == "-m" || a == "-M" || a == "-c" ||
                a == "-C" || a == "--delete" || a == "--move" ||
                a == "--copy" || a == "-u" || a == "--set-upstream-to" ||
                a == "--unset-upstream")
                return false;
        return true;
    }
    if (verb == "stash") return args.size() > 1 && args[1] == "list";
    return false;
}

// Backstop so a wedged git (e.g. a network op stuck mid-connection) can't hold
// g_git_mutex forever and freeze the app. Generous enough for normal
// push/pull/fetch; local ops finish in well under a second.
static constexpr int GIT_TIMEOUT_MS = 60000;

// The usual "hang" is git blocking on an interactive credential prompt; with no
// TTY it would wait out the whole timeout. Turn prompts into immediate failures
// so the user gets a toast right away instead of a 60s stall.
static void disable_git_prompts_once() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        setenv("GIT_TERMINAL_PROMPT", "0", 1);
        setenv("GIT_SSH_COMMAND", "ssh -oBatchMode=yes", 0);  // don't override
    });
}

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

// Run a sync git_* function on a detached background thread. Keeps the async
// wrappers as one-liners so argument lists live in one place (the sync fns).
template <class Fn, class... Args>
std::future<GitResult> spawn(Fn fn, Args... args) {
    std::packaged_task<GitResult()> task([=] { return fn(args...); });
    auto fut = task.get_future();
    std::thread(std::move(task)).detach();
    return fut;
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

    disable_git_prompts_once();

    GitResult result;
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    clock::time_point t1;
    if (is_read_only(args)) {
        std::shared_lock<std::shared_mutex> lock(g_git_mutex);
        t1 = clock::now();
        result.raw = run_process("", cmd, GIT_TIMEOUT_MS);
    } else {
        std::unique_lock<std::shared_mutex> lock(g_git_mutex);
        t1 = clock::now();
        result.raw = run_process("", cmd, GIT_TIMEOUT_MS);
    }
    const auto t2 = clock::now();
    auto ms = [](auto d) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
    };
    log_info("git: {} ms (waited {} ms for lock): git {}", ms(t2 - t1),
             ms(t1 - t0), args.empty() ? std::string() : args[0]);

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

// --- Startup prefetch ---

static std::mutex g_prefetch_mutex;
static std::unordered_map<std::string, PrefetchedReads> g_prefetched;

void prefetch_repo(const std::string& repo_path) {
    if (repo_path.empty()) return;
    std::lock_guard<std::mutex> lock(g_prefetch_mutex);
    if (g_prefetched.count(repo_path)) return;
    PrefetchedReads pre;
    pre.status   = git_status_async(repo_path);
    pre.log      = git_log_async(repo_path, 100, 0);
    pre.diff     = git_diff_async(repo_path);
    pre.branches = git_branch_list_async(repo_path);
    g_prefetched.emplace(repo_path, std::move(pre));
    log_info("prefetch: started reads for {}", repo_path);
}

bool take_prefetched(const std::string& repo_path, PrefetchedReads& out) {
    std::lock_guard<std::mutex> lock(g_prefetch_mutex);
    auto it = g_prefetched.find(repo_path);
    if (it == g_prefetched.end()) return false;
    out = std::move(it->second);
    g_prefetched.erase(it);
    return true;
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
// Each runs its sync counterpart on a background thread (see spawn()).

std::future<GitResult> git_status_async(const std::string& repo_path) {
    return spawn(git_status, repo_path);
}

std::future<GitResult> git_log_async(const std::string& repo_path,
                                      int max_count, int skip) {
    return spawn(git_log, repo_path, max_count, skip);
}

std::future<GitResult> git_diff_async(const std::string& repo_path) {
    return spawn(git_diff, repo_path);
}

std::future<GitResult> git_branch_list_async(
    const std::string& repo_path) {
    return spawn(git_branch_list, repo_path);
}

std::future<GitResult> git_rev_parse_head_async(
    const std::string& repo_path) {
    return spawn(git_rev_parse_head, repo_path);
}

}  // namespace git
