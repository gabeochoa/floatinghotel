#include "process.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>

extern char** environ;

namespace {

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

}  // namespace

ProcessResult run_process(const std::string& working_dir,
                          const std::vector<std::string>& args,
                          int timeout_ms, std::stop_token stop) {
    ProcessResult result;

    if (stop.stop_requested()) {
        result.cancelled = true;
        result.stderr_str = "Read cancelled";
        return result;
    }

    if (args.empty()) {
        result.stderr_str = "No command specified";
        return result;
    }

    int stdout_pipe[2];
    int stderr_pipe[2];
    if (pipe(stdout_pipe) != 0) {
        result.stderr_str = "Failed to create pipes";
        return result;
    }
    if (pipe(stderr_pipe) != 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        result.stderr_str = "Failed to create pipes";
        return result;
    }

    // run_process is called from several threads at once. A child spawned by
    // one thread inherits every fd the process has open at that instant,
    // including the pipes another thread just made for *its* child. That
    // sibling then holds our write end open, so we never see EOF until it
    // exits: four concurrent reads all "finished" at the moment the slowest
    // one did, and once wedged for good. Mark our pipes close-on-exec and ask
    // spawn to close everything not named in the file actions.
    for (int fd : {stdout_pipe[0], stdout_pipe[1], stderr_pipe[0], stderr_pipe[1]})
        fcntl(fd, F_SETFD, FD_CLOEXEC);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
#ifdef __APPLE__
    posix_spawn_file_actions_addinherit_np(&actions, STDIN_FILENO);
#endif

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    short flags = POSIX_SPAWN_SETPGROUP;
#ifdef __APPLE__
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
    posix_spawnattr_setflags(&attr, flags);
    posix_spawnattr_setpgroup(&attr, 0);

    if (!working_dir.empty()) {
        posix_spawn_file_actions_addchdir(&actions, working_dir.c_str());
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a : args) {
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid;
    int spawn_err =
        posix_spawnp(&pid, argv[0], &actions, &attr, argv.data(), environ);
    posix_spawnattr_destroy(&attr);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    if (spawn_err != 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        posix_spawn_file_actions_destroy(&actions);
        result.stderr_str =
            std::string("posix_spawnp failed: ") + strerror(spawn_err);
        return result;
    }

    // Non-blocking + poll so a child that never writes / never exits (e.g. git
    // waiting on a credential prompt) can be bounded and killed rather than
    // hanging read()/waitpid() forever.
    set_nonblocking(stdout_pipe[0]);
    set_nonblocking(stderr_pipe[0]);

    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
    bool timed_out = false;
    bool poll_failed = false;
    bool open_out = true, open_err = true;
    bool exited = false;
    int status = 0;
    std::array<char, 4096> buf;

    auto drain = [&](int fd, std::string& out, bool& open) {
        ssize_t n = -1;
        while (!stop.stop_requested() && (n = read(fd, buf.data(), buf.size())) > 0)
        {
            out.append(buf.data(), static_cast<size_t>(n));
            if (timeout_ms > 0 && clock::now() >= deadline) { timed_out = true; return; }
        }
        if (stop.stop_requested()) return;
        if (n == 0)
            open = false;  // EOF: write end closed
        else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            open = false;
    };

    while (open_out || open_err || !exited) {
        if (timed_out) break;
        if (stop.stop_requested()) { result.cancelled = true; break; }
        if (!exited) {
            auto waited = waitpid(pid, &status, WNOHANG);
            exited = waited == pid || (waited < 0 && errno == ECHILD);
        }
        if (!open_out && !open_err && exited) break;
        int wait_ms = 20;
        if (timeout_ms > 0) {
            auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(
                           deadline - clock::now())
                           .count();
            if (rem <= 0) { timed_out = true; break; }
            wait_ms = std::min(wait_ms, static_cast<int>(rem));
        }

        struct pollfd pfds[2];
        pfds[0] = {open_out ? stdout_pipe[0] : -1, POLLIN, 0};
        pfds[1] = {open_err ? stderr_pipe[0] : -1, POLLIN, 0};

        int pr = poll(pfds, 2, wait_ms);
        if (pr == 0) continue;
        if (pr < 0) {
            if (errno == EINTR) continue;
            poll_failed = true;
            result.stderr_str = std::string("poll failed: ") + strerror(errno);
            break;
        }
        if (open_out && (pfds[0].revents & (POLLIN | POLLHUP)))
            drain(stdout_pipe[0], result.stdout_str, open_out);
        if (open_err && (pfds[1].revents & (POLLIN | POLLHUP)))
            drain(stderr_pipe[0], result.stderr_str, open_err);
    }

    if (timed_out || result.cancelled || poll_failed) {
        kill(-pid, SIGKILL);
        if (!poll_failed) result.stderr_str = result.cancelled ? "Read cancelled" :
            "timed out after " + std::to_string(timeout_ms) + "ms";
    }

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    if (!exited) while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    result.exit_code =
        (timed_out || result.cancelled || poll_failed) ? -1 : (WIFEXITED(status) ? WEXITSTATUS(status) : -1);

    posix_spawn_file_actions_destroy(&actions);
    return result;
}

std::future<ProcessResult> run_process_async(
    const std::string& working_dir, const std::vector<std::string>& args,
    std::function<void(const std::string&)> on_output) {
    return std::async(std::launch::async, [working_dir, args, on_output]() {
        auto result = run_process(working_dir, args);
        if (on_output && !result.stdout_str.empty()) {
            on_output(result.stdout_str);
        }
        return result;
    });
}
