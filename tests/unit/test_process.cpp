// Unit tests for run_process.
//
// These tests invoke real system commands (echo, ls, false, etc.) so they
// require a POSIX environment -- which is fine for the macOS dev machines
// this project targets.

#include "test_framework.h"
#include "../../src/util/process.h"

#include <filesystem>

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

int main() {
    printf("=== process tests ===\n");
    RUN_ALL_TESTS();
}
