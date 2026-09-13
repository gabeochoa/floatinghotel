#include "test_framework.h"
#include "../../src/git/source_position.h"
#include "../../src/git/git_runner.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>

TEST(source_positions_validate_bounds_and_preserve_historical_identity) {
    char directory[] = "/tmp/fh-position.XXXXXX";
    auto* repo = mkdtemp(directory);
    ASSERT_TRUE(repo != nullptr);
    auto run = [&](std::vector<std::string> args) { auto result = git::git_run(repo, args); ASSERT_TRUE(result.success()); return result.stdout_str(); };
    run({"init", "-q", "-b", "main"}); run({"config", "user.name", "Position fixture"});
    run({"config", "user.email", "position@example.invalid"}); run({"config", "commit.gpgsign", "false"});
    const auto path = std::filesystem::path(repo) / "a.cpp";
    { std::ofstream out(path); for (int i = 1; i <= 5000; ++i) out << "line " << i << " 日本\r\n"; }
    run({"add", "."}); run({"commit", "-qm", "Original"});
    auto oid = run({"rev-parse", "HEAD"}); oid.pop_back();
    auto location = reading::source("a.cpp", "HEAD", 5000);
    location.column = 999;
    const auto last = git::locate_source_position(repo, location);
    ASSERT_TRUE(last.error.empty()); ASSERT_EQ(last.location.line, 5000);
    ASSERT_EQ(last.location.column, 13);
    ASSERT_EQ(reading::revision_text(last.location.destination.revision), oid);
    std::filesystem::remove(path);
    ASSERT_TRUE(git::locate_source_position(repo, reading::source("a.cpp", "INDEX", 5000)).error.empty());
    ASSERT_TRUE(git::locate_source_position(repo, location).error.empty());
    ASSERT_FALSE(git::locate_source_position(repo, reading::source("a.cpp", "", 1)).error.empty());
    location.line = 5001;
    ASSERT_FALSE(git::locate_source_position(repo, location).error.empty());
    std::stop_source stop; stop.request_stop();
    ASSERT_FALSE(git::locate_source_position(repo, reading::source("a.cpp", oid, 1), stop.get_token()).error.empty());
    { std::ofstream out(path, std::ios::binary); out.write("a\0b", 3); }
    ASSERT_FALSE(git::locate_source_position(repo, reading::source("a.cpp", "", 1)).error.empty());
    { std::ofstream out(path, std::ios::binary); out.write("a\0\n\0b\0", 6); }
    auto utf16 = reading::source("a.cpp", "", 2);
    utf16.column = 99;
    const auto decoded = git::locate_source_position(repo, utf16, {}, "utf16le");
    ASSERT_TRUE(decoded.error.empty()); ASSERT_EQ(decoded.location.column, 2);
    std::filesystem::remove_all(repo);
}

TEST(source_positions_resolve_columns_beyond_the_first_fragment) {
    char directory[] = "/tmp/fh-position-column.XXXXXX";
    auto* repo = mkdtemp(directory);
    ASSERT_TRUE(repo != nullptr);
    { std::ofstream out(std::filesystem::path(repo) / "a.cpp"); out << std::string(300000, ' ') << "é NEEDLE"; }
    auto location = reading::source("a.cpp", "", 1);
    location.column = 300003;
    auto found = git::locate_source_position(repo, location);
    ASSERT_TRUE(found.error.empty()); ASSERT_EQ(found.location.column, 300003);
    location.column = 900000;
    auto bounded = git::locate_source_position(repo, location);
    ASSERT_TRUE(bounded.error.empty()); ASSERT_EQ(bounded.location.column, 300009);
    std::filesystem::remove_all(repo);
}

int main() { RUN_ALL_TESTS(); }
