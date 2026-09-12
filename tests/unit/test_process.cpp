// Unit tests for run_process.
//
// These tests invoke real system commands (echo, ls, false, etc.) so they
// require a POSIX environment -- which is fine for the macOS dev machines
// this project targets.

#include "test_framework.h"
#include "../../src/util/process.h"

#include <filesystem>
#include <fstream>
#include <signal.h>
#include <thread>
#include <unistd.h>

TEST(process_empty_args) {
    auto r = run_process("", {});
    ASSERT_FALSE(r.success());
    ASSERT_STREQ(r.stderr_str, "No command specified");
}

TEST(process_echo_stdout) {
    auto r = run_process("", {"echo", "hello world"});
    ASSERT_TRUE(r.success());
    ASSERT_EQ(r.exit_code, 0);
    // echo appends a newline
    ASSERT_STREQ(r.stdout_str, "hello world\n");
}

TEST(process_exit_code_nonzero) {
    auto r = run_process("", {"false"});
    ASSERT_FALSE(r.success());
    ASSERT_NE(r.exit_code, 0);
}

TEST(process_exit_code_zero) {
    auto r = run_process("", {"true"});
    ASSERT_TRUE(r.success());
    ASSERT_EQ(r.exit_code, 0);
}

TEST(process_stderr_capture) {
    // Use sh -c to write to stderr
    auto r = run_process("", {"sh", "-c", "echo error_msg >&2; exit 1"});
    ASSERT_FALSE(r.success());
    ASSERT_STREQ(r.stderr_str, "error_msg\n");
}

TEST(process_working_directory) {
    // /tmp should exist on macOS
    auto r = run_process("/tmp", {"pwd"});
    ASSERT_TRUE(r.success());
    // macOS resolves /tmp -> /private/tmp
    std::string actual = r.stdout_str;
    // Remove trailing newline
    if (!actual.empty() && actual.back() == '\n') actual.pop_back();
    auto canonical = std::filesystem::canonical("/tmp").string();
    ASSERT_STREQ(actual, canonical);
}

TEST(process_multiple_args) {
    auto r = run_process("", {"printf", "%s-%s", "foo", "bar"});
    ASSERT_TRUE(r.success());
    ASSERT_STREQ(r.stdout_str, "foo-bar");
}

TEST(process_nonexistent_command) {
    auto r = run_process("", {"__nonexistent_command_xyz_12345__"});
    // posix_spawnp should fail
    ASSERT_FALSE(r.success());
}

TEST(process_large_output) {
    // Generate a reasonably large stdout to test pipe reading
    auto r = run_process("", {"sh", "-c", "seq 1 1000"});
    ASSERT_TRUE(r.success());
    // Verify it has content and ends with "1000\n"
    ASSERT_TRUE(r.stdout_str.size() > 100);
    std::string last_line = "1000\n";
    std::string tail = r.stdout_str.substr(r.stdout_str.size() - last_line.size());
    ASSERT_STREQ(tail, last_line);
}

TEST(process_async_basic) {
    auto future = run_process_async("", {"echo", "async_test"});
    auto r = future.get();
    ASSERT_TRUE(r.success());
    ASSERT_STREQ(r.stdout_str, "async_test\n");
}

TEST(process_timeout_still_applies_after_output_closes) {
    auto result = run_process("", {"sh", "-c", "exec 1>&- 2>&-; sleep 2"}, 50);
    ASSERT_FALSE(result.success());
    ASSERT_TRUE(result.stderr_str.find("timed out") != std::string::npos);
}

TEST(process_cancelled_before_spawn) {
    std::stop_source stop;
    stop.request_stop();
    auto result = run_process("", {"printf", "must not run"}, 1000, stop.get_token());
    ASSERT_TRUE(result.cancelled);
    ASSERT_FALSE(result.success());
    ASSERT_TRUE(result.stdout_str.empty());
}

TEST(process_cancellation_kills_descendants_and_reaps_the_child) {
    char directory[] = "/tmp/fh-cancel-test.XXXXXX";
    auto* path = mkdtemp(directory);
    ASSERT_TRUE(path != nullptr);
    auto marker = std::filesystem::path(path) / "child";
    std::stop_source stop;
    auto pending = std::async(std::launch::async, [&, token = stop.get_token()] {
        return run_process(path, {"sh", "-c", "sleep 10 & printf '%s' $! > child; wait"}, 10000, token);
    });
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!std::filesystem::exists(marker) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    pid_t child = 0;
    { std::ifstream input(marker); input >> child; }
    stop.request_stop();
    ASSERT_EQ(pending.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    ASSERT_TRUE(pending.get().cancelled);
    ASSERT_TRUE(child > 0);
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (kill(child, 0) == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_EQ(kill(child, 0), -1);
    std::filesystem::remove(marker);
    std::filesystem::remove(path);
}

TEST(process_cancel_after_output_closes) {
    std::stop_source stop;
    auto pending = std::async(std::launch::async, [token = stop.get_token()] {
        return run_process("", {"sh", "-c", "exec 1>&- 2>&-; sleep 5"}, 10000, token);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.request_stop();
    ASSERT_EQ(pending.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    ASSERT_TRUE(pending.get().cancelled);
}

TEST(streamed_output_stops_intentionally_without_retaining_full_output) {
    size_t bytes = 0;
    auto result = run_process("", {"sh", "-c", "while :; do printf xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx; done"}, 1000, {},
        [&](std::string_view chunk) { bytes += chunk.size(); return bytes < 100; });
    ASSERT_TRUE(result.outputStopped);
    ASSERT_FALSE(result.cancelled);
    ASSERT_FALSE(result.success());
    ASSERT_TRUE(result.stdout_str.empty());
    ASSERT_TRUE(bytes <= 4196);
}

TEST(streamed_output_preserves_cancellation_and_timeout_outcomes) {
    std::stop_source stop;
    auto cancelled = run_process("", {"sh", "-c", "printf hello; sleep 2"}, 1000, stop.get_token(),
        [&](std::string_view) { stop.request_stop(); return false; });
    ASSERT_TRUE(cancelled.cancelled);
    auto timeout = run_process("", {"sh", "-c", "printf hello; sleep 2"}, 20, {}, [](std::string_view) { return true; });
    ASSERT_FALSE(timeout.outputStopped);
    ASSERT_TRUE(timeout.stderr_str.find("timed out") != std::string::npos);
}

int main() {
    printf("=== process tests ===\n");
    RUN_ALL_TESTS();
}
