// Unit tests for Settings.
//
// Settings depends on afterhours singleton + files plugin (for config path).
// We test the class through its public API by creating an instance via the
// singleton macro, setting values, writing to disk, re-loading, and checking
// that the values round-trip correctly.
//
// To avoid polluting the real config directory, we:
//   1. Disable auto_save so setters don't trigger writes mid-test.
//   2. Write/read from a temp directory by manipulating the settings file
//      path through the public API (get_settings_path returns a path under
//      the files plugin config dir, which we initialize to a temp dir).

#include "test_framework.h"

// We need afterhours files plugin initialized so get_settings_path() works.
// Initialize it with a temp directory as the game name.
#include <afterhours/src/plugins/files.h>
#include "../../src/settings.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// ---- Tests ----

TEST(settings_default_values) {
    auto& s = Settings::get();
    // Defaults as per Settings::Data
    ASSERT_EQ(s.get_window_width(), 1200);
    ASSERT_EQ(s.get_window_height(), 800);
    ASSERT_EQ(s.get_window_x(), 100);
    ASSERT_EQ(s.get_window_y(), 100);
    ASSERT_TRUE(s.get_sidebar_width() > 200.0f);
    ASSERT_TRUE(s.get_open_repos().empty());
    ASSERT_STREQ(s.get_last_active_repo(), "");
    ASSERT_STREQ(s.get_unstaged_policy(), "ask");
}

TEST(settings_window_geometry) {
    auto& s = Settings::get();
    s.set_window_geometry(50, 60, 1920, 1080);
    ASSERT_EQ(s.get_window_x(), 50);
    ASSERT_EQ(s.get_window_y(), 60);
    ASSERT_EQ(s.get_window_width(), 1920);
    ASSERT_EQ(s.get_window_height(), 1080);
}

TEST(settings_sidebar_width) {
    auto& s = Settings::get();
    s.set_sidebar_width(350.0f);
    ASSERT_TRUE(s.get_sidebar_width() > 349.0f && s.get_sidebar_width() < 351.0f);
}

TEST(settings_commit_log_ratio_clamped) {
    auto& s = Settings::get();
    // Should clamp to [0.1, 0.9]
    s.set_commit_log_ratio(0.05f);
    ASSERT_TRUE(s.get_commit_log_ratio() >= 0.1f);

    s.set_commit_log_ratio(0.95f);
    ASSERT_TRUE(s.get_commit_log_ratio() <= 0.9f);

    s.set_commit_log_ratio(0.5f);
    ASSERT_TRUE(s.get_commit_log_ratio() > 0.49f && s.get_commit_log_ratio() < 0.51f);
}

TEST(settings_open_repos) {
    auto& s = Settings::get();
    // Clear existing
    while (!s.get_open_repos().empty()) {
        s.remove_open_repo(s.get_open_repos().front());
    }
    s.add_open_repo("/home/user/project_a");
    s.add_open_repo("/home/user/project_b");
    ASSERT_EQ(s.get_open_repos().size(), static_cast<size_t>(2));
    ASSERT_STREQ(s.get_open_repos()[0], "/home/user/project_a");
    ASSERT_STREQ(s.get_open_repos()[1], "/home/user/project_b");
}

TEST(settings_open_repos_no_duplicates) {
    auto& s = Settings::get();
    while (!s.get_open_repos().empty()) {
        s.remove_open_repo(s.get_open_repos().front());
    }
    s.add_open_repo("/path/one");
    s.add_open_repo("/path/two");
    s.add_open_repo("/path/one");  // duplicate
    ASSERT_EQ(s.get_open_repos().size(), static_cast<size_t>(2));
    // The duplicate should have been moved to the end
    ASSERT_STREQ(s.get_open_repos()[0], "/path/two");
    ASSERT_STREQ(s.get_open_repos()[1], "/path/one");
}

TEST(settings_open_repos_remove) {
    auto& s = Settings::get();
    while (!s.get_open_repos().empty()) {
        s.remove_open_repo(s.get_open_repos().front());
    }
    s.add_open_repo("/a");
    s.add_open_repo("/b");
    s.add_open_repo("/c");
    s.remove_open_repo("/b");
    ASSERT_EQ(s.get_open_repos().size(), static_cast<size_t>(2));
    ASSERT_STREQ(s.get_open_repos()[0], "/a");
    ASSERT_STREQ(s.get_open_repos()[1], "/c");
}

TEST(settings_add_empty_repo_ignored) {
    auto& s = Settings::get();
    size_t before = s.get_open_repos().size();
    s.add_open_repo("");
    ASSERT_EQ(s.get_open_repos().size(), before);
}

TEST(settings_last_active_repo) {
    auto& s = Settings::get();
    s.set_last_active_repo("/home/user/myrepo");
    ASSERT_STREQ(s.get_last_active_repo(), "/home/user/myrepo");
}

TEST(settings_unstaged_policy_valid) {
    auto& s = Settings::get();
    s.set_unstaged_policy("stage_all");
    ASSERT_STREQ(s.get_unstaged_policy(), "stage_all");
    s.set_unstaged_policy("staged_only");
    ASSERT_STREQ(s.get_unstaged_policy(), "staged_only");
    s.set_unstaged_policy("ask");
    ASSERT_STREQ(s.get_unstaged_policy(), "ask");
}

TEST(settings_unstaged_policy_invalid_resets_to_ask) {
    auto& s = Settings::get();
    s.set_unstaged_policy("invalid_value");
    ASSERT_STREQ(s.get_unstaged_policy(), "ask");
}

TEST(settings_write_and_load_roundtrip) {
    auto& s = Settings::get();
    s.auto_save_enabled = false;

    // Set specific values
    s.set_window_geometry(42, 43, 1600, 900);
    s.set_sidebar_width(300.0f);
    s.set_commit_log_ratio(0.6f);

    while (!s.get_open_repos().empty()) {
        s.remove_open_repo(s.get_open_repos().front());
    }
    s.add_open_repo("/test/repo1");
    s.add_open_repo("/test/repo2");
    s.set_last_active_repo("/test/repo2");
    s.set_unstaged_policy("stage_all");

    // Write
    s.write_save_file();

    // Verify file exists
    std::string path = s.get_settings_path();
    ASSERT_TRUE(fs::exists(path));

    // Tamper with in-memory values
    s.set_window_geometry(0, 0, 0, 0);
    s.set_sidebar_width(0.0f);

    // Reload
    bool loaded = s.load_save_file();
    ASSERT_TRUE(loaded);

    // Verify round-trip
    ASSERT_EQ(s.get_window_x(), 42);
    ASSERT_EQ(s.get_window_y(), 43);
    ASSERT_EQ(s.get_window_width(), 1600);
    ASSERT_EQ(s.get_window_height(), 900);
    ASSERT_TRUE(s.get_sidebar_width() > 299.0f && s.get_sidebar_width() < 301.0f);
    ASSERT_TRUE(s.get_commit_log_ratio() > 0.59f && s.get_commit_log_ratio() < 0.61f);
    ASSERT_EQ(s.get_open_repos().size(), static_cast<size_t>(2));
    ASSERT_STREQ(s.get_last_active_repo(), "/test/repo2");
    ASSERT_STREQ(s.get_unstaged_policy(), "stage_all");

    // Clean up
    fs::remove(path);
}

TEST(settings_load_missing_file_returns_false) {
    auto& s = Settings::get();
    // Make sure the file does not exist
    std::string path = s.get_settings_path();
    fs::remove(path);
    bool loaded = s.load_save_file();
    ASSERT_FALSE(loaded);
}

TEST(settings_load_malformed_json) {
    auto& s = Settings::get();
    std::string path = s.get_settings_path();
    // Write garbage
    {
        std::ofstream f(path);
        f << "{{{{not json at all";
    }
    bool loaded = s.load_save_file();
    ASSERT_FALSE(loaded);  // Should return false on parse error
    fs::remove(path);
}

TEST(settings_load_partial_json_uses_defaults) {
    auto& s = Settings::get();
    std::string path = s.get_settings_path();
    // Write valid JSON with only some fields
    {
        std::ofstream f(path);
        f << R"({"window_width": 2560, "window_height": 1440})";
    }
    bool loaded = s.load_save_file();
    ASSERT_TRUE(loaded);
    ASSERT_EQ(s.get_window_width(), 2560);
    ASSERT_EQ(s.get_window_height(), 1440);
    // Fields not in JSON should use defaults
    ASSERT_EQ(s.get_window_x(), 100);
    ASSERT_EQ(s.get_window_y(), 100);
    ASSERT_STREQ(s.get_unstaged_policy(), "ask");
    fs::remove(path);
}

int main() {
    // Initialize the files plugin with a temp directory so settings writes
    // go to an isolated location.
    afterhours::files::init("floatinghotel_test", "resources");

    auto& s = Settings::get();
    s.auto_save_enabled = false;

    printf("=== settings tests ===\n");
    RUN_ALL_TESTS();
}
