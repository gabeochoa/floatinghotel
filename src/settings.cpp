#include "settings.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include <afterhours/src/plugins/files.h>

#include <afterhours/src/logging.h>

struct Settings::Data {
    int windowWidth = 1200;
    int windowHeight = 800;
    int windowX = 100;
    int windowY = 100;
    float sidebarWidth = 280.0f;
    float commitLogRatio = 0.4f;
    std::vector<std::string> openRepos;
    std::string lastActiveRepo;
    std::string unstagedPolicy = "ask";
    std::vector<std::string> recentRepos;
};

Settings::Settings() { data_ = new Data(); }
Settings::~Settings() { delete data_; }

std::string Settings::get_settings_path() const {
    auto configDir = afterhours::files::get_config_path();
    if (configDir.empty()) {
        // files plugin not yet initialized; fall back to cwd
        return (std::filesystem::current_path() / "settings.json").string();
    }
    std::filesystem::create_directories(configDir);
    return (configDir / "settings.json").string();
}

bool Settings::load_save_file() {
    std::string path = get_settings_path();
    if (!std::filesystem::exists(path)) {
        log_info("No settings file found at {}, using defaults", path);
        return false;
    }

    try {
        std::ifstream f(path);
        nlohmann::json j = nlohmann::json::parse(f);

        data_->windowWidth = j.value("window_width", 1200);
        data_->windowHeight = j.value("window_height", 800);
        data_->windowX = j.value("window_x", 100);
        data_->windowY = j.value("window_y", 100);
        data_->sidebarWidth = j.value("sidebar_width", 280.0f);
        data_->commitLogRatio = j.value("commit_log_ratio", 0.4f);
        data_->openRepos =
            j.value("open_repos", std::vector<std::string>{});
        data_->lastActiveRepo = j.value("last_active_repo", std::string{});
        data_->unstagedPolicy = j.value("commit_unstaged_policy", std::string{"ask"});
        data_->recentRepos =
            j.value("recent_repos", std::vector<std::string>{});

        log_info("Settings loaded from {}", path);
        return true;
    } catch (const std::exception& e) {
        log_warn("Failed to parse settings file {}: {} (using defaults)",
                 path, e.what());
        return false;
    }
}

void Settings::write_save_file() {
    nlohmann::json j;
    j["window_width"] = data_->windowWidth;
    j["window_height"] = data_->windowHeight;
    j["window_x"] = data_->windowX;
    j["window_y"] = data_->windowY;
    j["sidebar_width"] = data_->sidebarWidth;
    j["commit_log_ratio"] = data_->commitLogRatio;
    j["open_repos"] = data_->openRepos;
    j["last_active_repo"] = data_->lastActiveRepo;
    j["commit_unstaged_policy"] = data_->unstagedPolicy;
    j["recent_repos"] = data_->recentRepos;

    std::string path = get_settings_path();
    std::ofstream f(path);
    if (!f.good()) {
        log_error("Failed to open settings file for writing: {}", path);
        return;
    }
    f << j.dump(2);
    log_info("Settings saved to {}", path);
}

void Settings::save_if_auto() {
    if (auto_save_enabled) {
        write_save_file();
    }
}

// Window geometry
int Settings::get_window_width() const { return data_->windowWidth; }
int Settings::get_window_height() const { return data_->windowHeight; }
int Settings::get_window_x() const { return data_->windowX; }
int Settings::get_window_y() const { return data_->windowY; }

void Settings::set_window_geometry(int x, int y, int w, int h) {
    data_->windowX = x;
    data_->windowY = y;
    data_->windowWidth = w;
    data_->windowHeight = h;
    save_if_auto();
}

// Layout
float Settings::get_sidebar_width() const { return data_->sidebarWidth; }

void Settings::set_sidebar_width(float w) {
    data_->sidebarWidth = w;
    save_if_auto();
}

float Settings::get_commit_log_ratio() const { return data_->commitLogRatio; }

void Settings::set_commit_log_ratio(float r) {
    data_->commitLogRatio = std::max(0.1f, std::min(0.9f, r));
    save_if_auto();
}

// Open repos
const std::vector<std::string>& Settings::get_open_repos() const {
    return data_->openRepos;
}

void Settings::add_open_repo(const std::string& path) {
    if (path.empty()) return;
    // Remove duplicates
    data_->openRepos.erase(
        std::remove(data_->openRepos.begin(), data_->openRepos.end(), path),
        data_->openRepos.end());
    data_->openRepos.push_back(path);
    save_if_auto();
}

void Settings::remove_open_repo(const std::string& path) {
    data_->openRepos.erase(
        std::remove(data_->openRepos.begin(), data_->openRepos.end(), path),
        data_->openRepos.end());
    save_if_auto();
}

// Last active repo
const std::string& Settings::get_last_active_repo() const {
    return data_->lastActiveRepo;
}

void Settings::set_last_active_repo(const std::string& path) {
    data_->lastActiveRepo = path;
    save_if_auto();
}

// Unstaged policy
std::string Settings::get_unstaged_policy() const {
    return data_->unstagedPolicy;
}

void Settings::set_unstaged_policy(const std::string& policy) {
    // Validate input
    if (policy == "ask" || policy == "stage_all" || policy == "staged_only") {
        data_->unstagedPolicy = policy;
    } else {
        data_->unstagedPolicy = "ask";
    }
    save_if_auto();
}

// Recent repos
std::vector<std::string> Settings::get_recent_repos() const {
    return data_->recentRepos;
}

void Settings::add_recent_repo(const std::string& path) {
    if (path.empty()) return;
    // Move to front (most recent first), remove duplicates
    data_->recentRepos.erase(
        std::remove(data_->recentRepos.begin(), data_->recentRepos.end(), path),
        data_->recentRepos.end());
    data_->recentRepos.insert(data_->recentRepos.begin(), path);
    // Keep max 10
    if (data_->recentRepos.size() > 10) {
        data_->recentRepos.resize(10);
    }
    save_if_auto();
}
