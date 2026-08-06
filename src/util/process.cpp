#include "process.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
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
                          int timeout_ms) {
    ProcessResult result;

    if (args.empty()) {
        result.stderr_str = "No command specified";
        return result;
    }

    int stdout_pipe[2];
    int stderr_pipe[2];
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        result.stderr_str = "Failed to create pipes";
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);

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
        posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);

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
    bool open_out = true, open_err = true;
    std::array<char, 4096> buf;

    auto drain = [&](int fd, std::string& out, bool& open) {
        ssize_t n;
        while ((n = read(fd, buf.data(), buf.size())) > 0)
            out.append(buf.data(), static_cast<size_t>(n));
        if (n == 0)
            open = false;  // EOF: write end closed
        else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            open = false;
    };

    while (open_out || open_err) {
        int wait_ms = -1;  // timeout_ms == 0 -> block indefinitely
        if (timeout_ms > 0) {
            auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(
                           deadline - clock::now())
                           .count();
            if (rem <= 0) { timed_out = true; break; }
            wait_ms = static_cast<int>(rem);
        }

        struct pollfd pfds[2];
        pfds[0] = {open_out ? stdout_pipe[0] : -1, POLLIN, 0};
        pfds[1] = {open_err ? stderr_pipe[0] : -1, POLLIN, 0};

        int pr = poll(pfds, 2, wait_ms);
        if (pr == 0) { timed_out = true; break; }
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (open_out && (pfds[0].revents & (POLLIN | POLLHUP)))
            drain(stdout_pipe[0], result.stdout_str, open_out);
        if (open_err && (pfds[1].revents & (POLLIN | POLLHUP)))
            drain(stderr_pipe[0], result.stderr_str, open_err);
    }

    if (timed_out) {
        kill(pid, SIGKILL);
        result.stderr_str =
            "timed out after " + std::to_string(timeout_ms / 1000) + "s";
    }

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    int status;
    waitpid(pid, &status, 0);  // reap; returns promptly after SIGKILL
    result.exit_code =
        timed_out ? -1 : (WIFEXITED(status) ? WEXITSTATUS(status) : -1);

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
