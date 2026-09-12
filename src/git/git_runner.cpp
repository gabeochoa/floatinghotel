#include "git_runner.h"
#include "repository_lock.h"

#include "../../vendor/afterhours/src/logging.h"

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <shared_mutex>
#include <thread>

namespace git {

static LogCallback g_log_callback = nullptr;
static std::mutex g_log_mutex;

// Commands that never touch the index or refs, so they may overlap each other
// and only need to be kept apart from writes.
static bool is_read_only(const std::vector<std::string>& args) {
    if (args.empty()) return false;
    const std::string& verb = args[0];
    if (verb == "status" || verb == "log" || verb == "diff" || verb == "grep" || verb == "blame" ||
        verb == "rev-parse" || verb == "show" || verb == "for-each-ref" || verb == "merge-base" ||
        verb == "ls-files" || verb == "ls-tree" || verb == "cat-file" || verb == "rev-list" ||
        verb == "range-diff" || verb == "ls-remote")
        return true;
    if (verb == "remote") return args.size() == 1 ||
        (args.size() == 2 && (args[1] == "-v" || args[1] == "--verbose"));
    if (verb == "branch") {
        for (size_t i = 1; i < args.size(); ++i) {
            const auto& a = args[i];
            if (a != "-a" && a != "-r" && a != "-v" && a != "-vv" && a != "--list" &&
                a != "--all" && a != "--remotes" && a != "--show-current" && a != "--no-color" &&
                !a.starts_with("--format=") && !a.starts_with("--sort="))
                return false;
        }
        return true;
    }
    if (verb == "stash") return args.size() > 1 && args[1] == "list";
    return false;
}

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

void set_log_callback(LogCallback cb) {
    std::lock_guard lock(g_log_mutex);
    g_log_callback = std::move(cb);
}

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

template <class Fn, class... Args>
async_work::Task<GitResult> spawn(Fn fn, Args... args) {
    return async_work::launch([=](std::stop_token stop) { return fn(args..., stop); },
        async_work::Priority::Background, GitResult{{"", "Background queue is full; refresh to retry", -1}});
}

}  // namespace

async_work::Task<RevisionComparison> git_compare_async(const std::string& repo,
    const std::string& base, const std::string& target, bool mergeBase,
    int context, bool ignoreWhitespace) {
    return async_work::launch([=](std::stop_token stop) {
        RevisionComparison out;
        auto resolve = [&](const std::string& revision, std::string& hash) {
            out.patch = git_run(repo, {"rev-parse", "--verify", "--end-of-options", revision + "^{commit}"}, stop);
            if (!out.patch.success()) return false;
            hash = out.patch.stdout_str();
            while (!hash.empty() && (hash.back() == '\n' || hash.back() == '\r')) hash.pop_back();
            return true;
        };
        if (!resolve(base, out.base) || !resolve(target, out.target)) return out;
        if (mergeBase) {
            out.patch = git_run(repo, {"merge-base", out.base, out.target}, stop);
            if (!out.patch.success()) return out;
            out.base = out.patch.stdout_str();
            while (!out.base.empty() && (out.base.back() == '\n' || out.base.back() == '\r')) out.base.pop_back();
        }
        std::vector<std::string> args{"diff", "--no-ext-diff", "--find-renames", "--unified=" + std::to_string(context)};
        if (ignoreWhitespace) args.push_back("--ignore-all-space");
        args.insert(args.end(), {out.base, out.target, "--"});
        out.patch = git_run(repo, args, stop);
        return out;
    }, async_work::Priority::Foreground,
        RevisionComparison{GitResult{{"", "Background queue is full; compare again to retry", -1}}, {}, {}});
}

GitResult git_run(const std::string& repo_path,
                  const std::vector<std::string>& args, std::stop_token stop,
                  std::function<bool(std::string_view)> consumeOutput) {
    std::vector<std::string> cmd = {"git"};
    if (!repo_path.empty()) {
        cmd.push_back("-C");
        cmd.push_back(repo_path);
    }
    const bool readOnly = is_read_only(args);
    if (readOnly) cmd.push_back("--no-optional-locks");
    cmd.insert(cmd.end(), args.begin(), args.end());

    disable_git_prompts_once();

    GitResult result;
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    clock::time_point t1;
    auto repositoryMutex = repository_mutex(repo_path);
    if (readOnly) {
        std::shared_lock<std::shared_timed_mutex> lock(*repositoryMutex, std::defer_lock);
        while (!lock.try_lock_for(std::chrono::milliseconds(20))) {
            if (stop.stop_requested()) {
                result.raw.cancelled = true;
                result.raw.stderr_str = "Read cancelled";
                return result;
            }
        }
        t1 = clock::now();
        result.raw = run_process("", cmd, GIT_TIMEOUT_MS, stop, std::move(consumeOutput));
    } else {
        std::unique_lock<std::shared_timed_mutex> lock(*repositoryMutex);
        t1 = clock::now();
        result.raw = run_process("", cmd, GIT_TIMEOUT_MS);
    }
    const auto t2 = clock::now();
    auto ms = [](auto d) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
    };
    log_info("git: {} ms (waited {} ms for lock): git {}", ms(t2 - t1),
             ms(t1 - t0), args.empty() ? std::string() : args[0]);

    {
        std::lock_guard lock(g_log_mutex);
        if (g_log_callback) {
            if (result.raw.outputStopped && !result.raw.cancelled)
                g_log_callback(build_command_string(cmd),
                               "Output capture stopped at the requested output limit",
                               result.stderr_str(), true);
            else
                g_log_callback(build_command_string(cmd), result.stdout_str(),
                               result.stderr_str(), result.success());
        }
    }

    return result;
}

async_work::Task<GitResult> git_run_async(
    const std::string& repo_path,
    const std::vector<std::string>& args, async_work::Priority priority) {
    return async_work::launch([repo_path, args](std::stop_token stop) {
        return git_run(repo_path, args, stop);
    }, priority, GitResult{{"", "Background queue is full; retry the action", -1}}, is_read_only(args));
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

GitResult git_status(const std::string& repo_path, std::stop_token stop) {
    return git_run(repo_path,
                   {"status", "--porcelain=v2", "--branch"}, stop);
}

GitResult git_log(const std::string& repo_path, int max_count, int skip, std::stop_token stop) {
    // Machine-readable format with NUL separators:
    // hash\0shortHash\0subject\0author\0date\0decorations\0parentHashes
    std::vector<std::string> args = {
        "log",
        "--topo-order",
        "--format=%H%x00%h%x00%s%x00%an%x00%aI%x00%D%x00%P",
    };
    if (max_count > 0) {
        args.push_back("-" + std::to_string(max_count));
    }
    if (skip > 0) {
        args.push_back("--skip=" + std::to_string(skip));
    }
    return git_run(repo_path, args, stop);
}

GitResult git_diff(const std::string& repo_path, std::stop_token stop) {
    return git_run(repo_path, {"diff"}, stop);
}

GitResult git_commit(const std::string& repo_path,
                     const std::string& message) {
    return git_run(repo_path, {"commit", "-m", message});
}

GitResult git_branch_list(const std::string& repo_path, std::stop_token stop) {
    // Machine-readable branch listing:
    // refname|objectname|HEAD|upstream|upstream_track
    return git_run(
        repo_path,
        {"branch", "--list",
         "--format=%(refname:short)|%(objectname:short)"
                   "|%(HEAD)|%(upstream:short)|%(upstream:track)"}, stop);
}

GitResult git_rev_parse_head(const std::string& repo_path, std::stop_token stop) {
    return git_run(repo_path, {"rev-parse", "HEAD"}, stop);
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

async_work::Task<GitResult> git_status_async(const std::string& repo_path) {
    return spawn(git_status, repo_path);
}

async_work::Task<GitResult> git_log_async(const std::string& repo_path,
                                      int max_count, int skip) {
    return spawn(git_log, repo_path, max_count, skip);
}

async_work::Task<GitResult> git_diff_async(const std::string& repo_path) {
    return spawn(git_diff, repo_path);
}

async_work::Task<GitResult> git_branch_list_async(
    const std::string& repo_path) {
    return spawn(git_branch_list, repo_path);
}

async_work::Task<GitResult> git_rev_parse_head_async(
    const std::string& repo_path) {
    return spawn(git_rev_parse_head, repo_path);
}

}  // namespace git
