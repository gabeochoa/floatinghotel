#pragma once

#include <afterhours/src/singleton.h>

#include <string>
#include <vector>

SINGLETON_FWD(Settings)
struct Settings {
    SINGLETON(Settings)

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

    // Layout
    float get_sidebar_width() const;
    void set_sidebar_width(float w);

    float get_commit_log_ratio() const;
    void set_commit_log_ratio(float r);

    // Open repos
    const std::vector<std::string>& get_open_repos() const;
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

    std::string get_settings_path() const;

    // Auto-save support
    bool auto_save_enabled = true;
    void save_if_auto();

private:
    struct Data;
    Data* data_;
};
