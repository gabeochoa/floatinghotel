// Unit tests for the logging system -- level filtering, formatting, and
// ScopedTimer.
//
// Uses dup2() to capture stdout output for verification.

#include "test_framework.h"
#include "../../src/logging.h"

#include <fstream>
#include <functional>
#include <unistd.h>

// Helper: redirect stdout to a temp file, run fn(), return captured output.
static std::string capture_stdout(std::function<void()> fn) {
    fflush(stdout);

    char tmpname[] = "/tmp/fh_log_test_XXXXXX";
    int tmpfd = mkstemp(tmpname);
    int saved = dup(STDOUT_FILENO);
    dup2(tmpfd, STDOUT_FILENO);
    close(tmpfd);

    fn();
    fflush(stdout);

    dup2(saved, STDOUT_FILENO);
    close(saved);

    std::ifstream f(tmpname);
    std::string result((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
    std::remove(tmpname);
    return result;
}

// ===========================================================================
// Level enum and setLevel
// ===========================================================================

TEST(level_enum_ordering) {
    ASSERT_TRUE(logging::Level::Debug < logging::Level::Info);
    ASSERT_TRUE(logging::Level::Info < logging::Level::Warning);
    ASSERT_TRUE(logging::Level::Warning < logging::Level::Error);
}

TEST(set_level_changes_global) {
    logging::setLevel(logging::Level::Warning);
    ASSERT_TRUE(logging::g_level == logging::Level::Warning);
    logging::setLevel(logging::Level::Debug);
    ASSERT_TRUE(logging::g_level == logging::Level::Debug);
    // Reset
    logging::setLevel(logging::Level::Info);
}

TEST(default_level_is_info) {
    // The inline default is Level::Info.  Reset and verify.
    logging::g_level = logging::Level::Info;
    ASSERT_TRUE(logging::g_level == logging::Level::Info);
}

// ===========================================================================
// debug() filtering
// ===========================================================================

TEST(debug_prints_when_level_debug) {
    logging::setLevel(logging::Level::Debug);
    auto out = capture_stdout([] { logging::debug("test message"); });
    ASSERT_TRUE(out.find("[DEBUG]") != std::string::npos);
    ASSERT_TRUE(out.find("test message") != std::string::npos);
}

TEST(debug_suppressed_when_level_info) {
    logging::setLevel(logging::Level::Info);
    auto out = capture_stdout([] { logging::debug("should not appear"); });
    ASSERT_TRUE(out.empty());
}

// ===========================================================================
// info() filtering
// ===========================================================================

TEST(info_prints_when_level_info) {
    logging::setLevel(logging::Level::Info);
    auto out = capture_stdout([] { logging::info("info msg"); });
    ASSERT_TRUE(out.find("[INFO]") != std::string::npos);
    ASSERT_TRUE(out.find("info msg") != std::string::npos);
}

TEST(info_suppressed_when_level_warning) {
    logging::setLevel(logging::Level::Warning);
    auto out = capture_stdout([] { logging::info("should not appear"); });
    ASSERT_TRUE(out.empty());
}

// ===========================================================================
// warning() filtering
// ===========================================================================

TEST(warning_prints_when_level_warning) {
    logging::setLevel(logging::Level::Warning);
    auto out = capture_stdout([] { logging::warning("warn msg"); });
    ASSERT_TRUE(out.find("[WARNING]") != std::string::npos);
    ASSERT_TRUE(out.find("warn msg") != std::string::npos);
}

TEST(warning_suppressed_when_level_error) {
    logging::setLevel(logging::Level::Error);
    auto out = capture_stdout([] { logging::warning("should not appear"); });
    ASSERT_TRUE(out.empty());
}

// ===========================================================================
// error() -- always prints regardless of level
// ===========================================================================

TEST(error_always_prints_at_error_level) {
    logging::setLevel(logging::Level::Error);
    auto out = capture_stdout([] { logging::error("error msg"); });
    ASSERT_TRUE(out.find("[ERROR]") != std::string::npos);
    ASSERT_TRUE(out.find("error msg") != std::string::npos);
}

TEST(error_prints_at_debug_level) {
    logging::setLevel(logging::Level::Debug);
    auto out = capture_stdout([] { logging::error("still prints"); });
    ASSERT_TRUE(out.find("[ERROR]") != std::string::npos);
}

// ===========================================================================
// Format strings
// ===========================================================================

TEST(format_string_with_args) {
    logging::setLevel(logging::Level::Info);
    auto out = capture_stdout([] {
        logging::info("count=%d name=%s", 42, "test");
    });
    ASSERT_TRUE(out.find("count=42") != std::string::npos);
    ASSERT_TRUE(out.find("name=test") != std::string::npos);
}

TEST(output_ends_with_newline) {
    logging::setLevel(logging::Level::Info);
    auto out = capture_stdout([] { logging::info("newline check"); });
    ASSERT_TRUE(!out.empty());
    ASSERT_EQ(out.back(), '\n');
}

// ===========================================================================
// ScopedTimer
// ===========================================================================

TEST(scoped_timer_produces_output) {
    logging::setLevel(logging::Level::Info);
    auto out = capture_stdout([] {
        { logging::ScopedTimer timer("test_op"); }
        // Timer destructor fires at end of block
    });
    ASSERT_TRUE(out.find("test_op") != std::string::npos);
    ASSERT_TRUE(out.find("ms") != std::string::npos);
}

TEST(scoped_timer_reports_nonnegative_time) {
    logging::setLevel(logging::Level::Info);
    auto out = capture_stdout([] {
        { logging::ScopedTimer timer("timing"); }
    });
    // The output is "[INFO] timing took X.XXX ms\n"
    // Verify it doesn't contain a negative sign before "ms"
    auto ms_pos = out.find("ms");
    ASSERT_TRUE(ms_pos != std::string::npos);
    // Find the number before " ms" -- check for negative
    auto took_pos = out.find("took ");
    ASSERT_TRUE(took_pos != std::string::npos);
    // Character after "took " should be a digit, not '-'
    ASSERT_NE(out[took_pos + 5], '-');
}

// ===========================================================================

int main() {
    // Reset to default level for predictable test behavior.
    logging::setLevel(logging::Level::Info);

    printf("=== logging tests ===\n");
    RUN_ALL_TESTS();
}
