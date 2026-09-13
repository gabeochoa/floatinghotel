#pragma once

#include <afterhours/src/singleton.h>

#include <string>
#include <vector>

#include "util/code_bookmark.h"
#include "util/reading_session.h"
#include "util/review_files.h"

SINGLETON_FWD(Settings)
struct Settings {
    SINGLETON(Settings)

    static constexpr float kDefaultCodeFontSize = 17.6f;

    Settings();
    ~Settings();

    Settings(const Settings&) = delete;
    void operator=(const Settings&) = delete;

    bool load_save_file();
    void write_save_file();

    // Window geometry
    int get_window_width() const;
    int get_window_height() const;
    int get_window_x() const;
    int get_window_y() const;
    void set_window_geometry(int x, int y, int w, int h);
    bool get_window_collapsed() const;
    int get_expanded_window_width() const;
    void remember_window_size(int width, int height, bool collapsed, int expandedWidth, float sidebarWidth);

    // Layout
    float get_sidebar_width() const;
    void set_sidebar_width(float w);

    float get_commit_log_ratio() const;
    void set_commit_log_ratio(float r);

    float get_code_font_size() const;
    void set_code_font_size(float size);

    // Open repos (tab session persistence)
    const std::vector<std::string>& get_open_repos() const;
    void set_open_repos(const std::vector<std::string>& repos);
    void add_open_repo(const std::string& path);
    void remove_open_repo(const std::string& path);

    // Last active repo
    const std::string& get_last_active_repo() const;
    void set_last_active_repo(const std::string& path);

    // Unstaged policy preference (T030)
    // Returns "ask", "stage_all", or "staged_only"
    std::string get_unstaged_policy() const;
    void set_unstaged_policy(const std::string& policy);

    // Recent repos (for welcome screen)
    std::vector<std::string> get_recent_repos() const;
    void add_recent_repo(const std::string& path);

    const std::vector<CodeBookmark>& get_code_bookmarks(const std::string& repoPath) const;
    void set_code_bookmarks(const std::string& repoPath,
                            const std::vector<CodeBookmark>& bookmarks);

    const reading::ReadingSession* get_reading_session(const std::string& repoPath) const;
    void set_reading_session(const std::string& repoPath, reading::ReadingSession session);

    review_files::DisplayMode get_review_display_mode(const std::string& repoPath) const;
    void set_review_display_mode(const std::string& repoPath, review_files::DisplayMode mode);

    std::string get_settings_path() const;

    // Auto-save support
    bool auto_save_enabled = true;
    void save_if_auto();

private:
    struct Data;
    Data* data_;
};
