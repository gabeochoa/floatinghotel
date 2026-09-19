#include "settings.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include <nlohmann/json.hpp>

#include <afterhours/src/plugins/files.h>

#include <afterhours/src/logging.h>

struct Settings::Data {
    int windowWidth = 280;
    int expandedWindowWidth = 1200;
    bool windowCollapsed = true;
    int windowHeight = 800;
    int windowX = 100;
    int windowY = 100;
    float sidebarWidth = 280.0f;
    float commitLogRatio = 0.4f;
    float codeFontSize = kDefaultCodeFontSize;
    float commandLogHeight = 200.f;
    std::vector<std::string> openRepos;
    std::string lastActiveRepo;
    std::string unstagedPolicy = "ask";
    std::vector<std::string> recentRepos;
    std::vector<std::string> pinnedRepos;
    std::map<std::string, std::string> repositoryHeads;
    std::map<std::string, std::set<std::string>> collapsedSections;
    std::map<std::string, std::vector<CodeBookmark>> codeBookmarks;
    std::map<std::string, reading::ReadingSession> readingSessions;
    std::map<std::string, review_files::DisplayMode> reviewDisplayModes;
};

Settings::Settings() { data_ = new Data(); }
Settings::~Settings() { delete data_; }

static float bounded_code_font_size(float size) {
    return std::isfinite(size) ? std::clamp(size, 10.f, 24.f) : Settings::kDefaultCodeFontSize;
}

std::string Settings::get_settings_path() const {
    auto configDir = afterhours::files::get_config_path();
    if (configDir.empty()) {
        // files plugin not yet initialized; fall back to cwd
        return (std::filesystem::current_path() / "settings.json").string();
    }
    std::error_code error;
    std::filesystem::create_directories(configDir, error);
    return (configDir / "settings.json").string();
}

bool Settings::load_save_file() {
    pendingSave_.reset();
    std::string path = get_settings_path();
    loadError.clear();
    std::error_code fileError;
    if (!std::filesystem::exists(path, fileError) && !fileError) {
        log_info("No settings file found at {}, using defaults", path);
        return false;
    }

    Data previous = *data_;
    try {
        std::ifstream f(path);
        nlohmann::json j = nlohmann::json::parse(f);
        if (!j.is_object()) throw std::runtime_error("Settings must be an object");
        if (j.value("schema_version", 0) > 1) throw std::runtime_error("Settings were written by a newer version");
        data_->repositoryHeads = j.value("repository_heads", std::map<std::string, std::string>{});
        data_->pinnedRepos = j.value("pinned_repos", std::vector<std::string>{});
        data_->collapsedSections = j.value("collapsed_sections", std::map<std::string, std::set<std::string>>{});

        data_->windowHeight = std::clamp(j.value("window_height", 800), 200, 16384);
        data_->windowX = j.value("window_x", 100);
        data_->windowY = j.value("window_y", 100);
        data_->sidebarWidth = j.value("sidebar_width", 280.0f);
        if (!std::isfinite(data_->sidebarWidth)) data_->sidebarWidth = 280.f;
        data_->sidebarWidth = std::clamp(data_->sidebarWidth, 200.f, 16384.f);
        data_->windowCollapsed = j.value("window_shelf_collapsed", true);
        data_->windowWidth = std::clamp(j.contains("window_shelf_collapsed") ? j.value("window_width", 280) :
            static_cast<int>(data_->sidebarWidth), 200, 16384);
        data_->expandedWindowWidth = std::clamp(j.value("expanded_window_width", 1200), 648, 16384);
        data_->commitLogRatio = j.value("commit_log_ratio", 0.4f);
        const float logHeight = j.value("command_log_height", 200.f);
        data_->commandLogHeight = std::isfinite(logHeight) ? std::clamp(logHeight, 80.f, 16384.f) : 200.f;
        data_->codeFontSize = bounded_code_font_size(j.value("code_font_size", kDefaultCodeFontSize));
        data_->openRepos =
            j.value("open_repos", std::vector<std::string>{});
        data_->lastActiveRepo = j.value("last_active_repo", std::string{});
        data_->unstagedPolicy = j.value("commit_unstaged_policy", std::string{"ask"});
        data_->recentRepos =
            j.value("recent_repos", std::vector<std::string>{});
        data_->reviewDisplayModes.clear();
        if (j.contains("review_display_modes") && j["review_display_modes"].is_object())
            for (const auto& [repo, value] : j["review_display_modes"].items())
                if (value == "all" || value == "selected") data_->reviewDisplayModes[repo] =
                    value == "all" ? review_files::DisplayMode::AllFiles : review_files::DisplayMode::SelectedFile;
        data_->readingSessions.clear();
        if (j.contains("reading_sessions") && j["reading_sessions"].is_object()) {
            for (const auto& [repo, value] : j["reading_sessions"].items())
                if (auto session = reading::decode_session(value)) data_->readingSessions.emplace(repo, std::move(*session));
        }
        data_->codeBookmarks.clear();
        if (j.contains("code_bookmarks") && j["code_bookmarks"].is_object()) {
            for (const auto& [repo, values] : j["code_bookmarks"].items()) {
                if (!values.is_array()) continue;
                auto& list = data_->codeBookmarks[repo];
                for (const auto& value : values) {
                    if (!value.is_object()) continue;
                    if (!value.contains("path") || !value["path"].is_string()) continue;
                    if (value.contains("revision") && !value["revision"].is_string()) continue;
                    if (value.contains("label") && !value["label"].is_string()) continue;
                    if (value.contains("line") && !value["line"].is_number_integer()) continue;
                    CodeBookmark bookmark;
                    bookmark.path = value.value("path", std::string{});
                    bookmark.revision = value.value("revision", std::string{});
                    bookmark.line = std::max(1, value.value("line", 1));
                    bookmark.label = value.value("label", std::string{});
                    if (!bookmark.path.empty()) list.push_back(std::move(bookmark));
                }
            }
        }

        log_info("Settings loaded from {}", path);
        return true;
    } catch (const std::exception& e) {
        *data_ = std::move(previous);
        loadError = std::string("Settings could not be loaded: ") + e.what();
        log_warn("Failed to parse settings file {}: {} (preserving existing settings)",
                 path, e.what());
        return false;
    }
}

void Settings::write_save_file() {
    pendingSave_.reset();
    if (!loadError.empty()) { saveError = "Settings file was not overwritten: " + loadError; return; }
    nlohmann::json j;
    j["schema_version"] = 1;
    j["repository_heads"] = data_->repositoryHeads;
    j["pinned_repos"] = data_->pinnedRepos;
    j["collapsed_sections"] = data_->collapsedSections;
    j["window_width"] = data_->windowWidth;
    j["window_height"] = data_->windowHeight;
    j["window_shelf_collapsed"] = data_->windowCollapsed;
    j["expanded_window_width"] = data_->expandedWindowWidth;
    j["window_x"] = data_->windowX;
    j["window_y"] = data_->windowY;
    j["sidebar_width"] = data_->sidebarWidth;
    j["commit_log_ratio"] = data_->commitLogRatio;
    j["code_font_size"] = data_->codeFontSize;
    j["command_log_height"] = data_->commandLogHeight;
    j["open_repos"] = data_->openRepos;
    j["last_active_repo"] = data_->lastActiveRepo;
    j["commit_unstaged_policy"] = data_->unstagedPolicy;
    j["recent_repos"] = data_->recentRepos;
    nlohmann::json bookmarks = nlohmann::json::object();
    for (const auto& [repo, values] : data_->codeBookmarks) {
        nlohmann::json list = nlohmann::json::array();
        for (const auto& bookmark : values) {
            list.push_back({
                {"path", bookmark.path},
                {"revision", bookmark.revision},
                {"line", bookmark.line},
                {"label", bookmark.label},
            });
        }
        bookmarks[repo] = std::move(list);
    }
    j["code_bookmarks"] = std::move(bookmarks);
    j["review_display_modes"] = nlohmann::json::object();
    for (const auto& [repo, mode] : data_->reviewDisplayModes)
        j["review_display_modes"][repo] = mode == review_files::DisplayMode::AllFiles ? "all" : "selected";
    j["reading_sessions"] = nlohmann::json::object();
    for (const auto& [repo, session] : data_->readingSessions)
        j["reading_sessions"][repo] = reading::encode_session(session);

    std::string path = get_settings_path();
    if (!afterhours::files::write_string_atomic(path, j.dump(2))) {
        saveError = "Failed to save settings: " + path;
        log_error("{}", saveError);
        return;
    }
    ++saveWriteCount_;
    saveError.clear();
    log_info("Settings saved to {}", path);
}

void Settings::save_if_auto() {
    if (auto_save_enabled) pendingSave_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
}

void Settings::flush_pending_save(std::chrono::steady_clock::time_point now) {
    if (auto_save_enabled && pendingSave_ && now >= *pendingSave_) write_save_file();
}

float Settings::get_command_log_height() const { return data_->commandLogHeight; }

void Settings::set_command_log_height(float height) {
    height = std::isfinite(height) ? std::clamp(height, 80.f, 16384.f) : 200.f;
    if (data_->commandLogHeight == height) return;
    data_->commandLogHeight = height;
    save_if_auto();
}

float Settings::get_code_font_size() const { return data_->codeFontSize; }

void Settings::set_code_font_size(float size) {
    size = bounded_code_font_size(size);
    if (data_->codeFontSize == size) return;
    data_->codeFontSize = size;
    save_if_auto();
}

// Window geometry
int Settings::get_window_width() const { return data_->windowWidth; }
int Settings::get_window_height() const { return data_->windowHeight; }
int Settings::get_window_x() const { return data_->windowX; }
int Settings::get_window_y() const { return data_->windowY; }

void Settings::set_window_geometry(int x, int y, int w, int h) {
    if (data_->windowX == x && data_->windowY == y && data_->windowWidth == w && data_->windowHeight == h) return;
    data_->windowX = x;
    data_->windowY = y;
    data_->windowWidth = w;
    data_->windowHeight = h;
    save_if_auto();
}

bool Settings::get_window_collapsed() const { return data_->windowCollapsed; }
int Settings::get_expanded_window_width() const { return data_->expandedWindowWidth; }

void Settings::remember_window_size(int width, int height, bool collapsed, int expandedWidth, float sidebarWidth) {
    if (width <= 0 || height <= 0) return;
    if (data_->windowWidth == width && data_->windowHeight == height && data_->windowCollapsed == collapsed &&
        data_->expandedWindowWidth == expandedWidth && data_->sidebarWidth == sidebarWidth) return;
    data_->windowWidth = width;
    data_->windowHeight = height;
    data_->windowCollapsed = collapsed;
    data_->expandedWindowWidth = expandedWidth;
    data_->sidebarWidth = sidebarWidth;
    save_if_auto();
}

// Layout
float Settings::get_sidebar_width() const { return data_->sidebarWidth; }

void Settings::set_sidebar_width(float w) {
    if (data_->sidebarWidth == w) return;
    data_->sidebarWidth = w;
    save_if_auto();
}

float Settings::get_commit_log_ratio() const { return data_->commitLogRatio; }

void Settings::set_commit_log_ratio(float r) {
    r = std::isfinite(r) ? std::clamp(r, 0.1f, 0.9f) : 0.4f;
    if (data_->commitLogRatio == r) return;
    data_->commitLogRatio = r;
    save_if_auto();
}

// Open repos
const std::vector<std::string>& Settings::get_open_repos() const {
    return data_->openRepos;
}

void Settings::set_open_repos(const std::vector<std::string>& repos) {
    if (data_->openRepos == repos) return;
    data_->openRepos = repos;
    save_if_auto();
}

void Settings::add_open_repo(const std::string& path) {
    if (path.empty() || (!data_->openRepos.empty() && data_->openRepos.back() == path)) return;
    // Remove duplicates
    data_->openRepos.erase(
        std::remove(data_->openRepos.begin(), data_->openRepos.end(), path),
        data_->openRepos.end());
    data_->openRepos.push_back(path);
    save_if_auto();
}

void Settings::remove_open_repo(const std::string& path) {
    if (std::find(data_->openRepos.begin(), data_->openRepos.end(), path) == data_->openRepos.end()) return;
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
    if (data_->lastActiveRepo == path) return;
    data_->lastActiveRepo = path;
    save_if_auto();
}

// Unstaged policy
std::string Settings::get_unstaged_policy() const {
    return data_->unstagedPolicy;
}

void Settings::set_unstaged_policy(const std::string& policy) {
    const auto value = policy == "ask" || policy == "stage_all" || policy == "staged_only" ? policy : "ask";
    if (data_->unstagedPolicy == value) return;
    data_->unstagedPolicy = value;
    save_if_auto();
}

// Recent repos
std::vector<std::string> Settings::get_recent_repos() const {
    return data_->recentRepos;
}

void Settings::add_recent_repo(const std::string& path) {
    if (path.empty() || (!data_->recentRepos.empty() && data_->recentRepos.front() == path)) return;
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

const std::vector<CodeBookmark>& Settings::get_code_bookmarks(const std::string& repoPath) const {
    static const std::vector<CodeBookmark> empty;
    auto it = data_->codeBookmarks.find(repoPath);
    return it == data_->codeBookmarks.end() ? empty : it->second;
}

void Settings::set_code_bookmarks(const std::string& repoPath,
                                  const std::vector<CodeBookmark>& bookmarks) {
    if (repoPath.empty() || get_code_bookmarks(repoPath) == bookmarks) return;
    data_->codeBookmarks[repoPath] = bookmarks;
    save_if_auto();
}

const reading::ReadingSession* Settings::get_reading_session(const std::string& repoPath) const {
    const auto found = data_->readingSessions.find(repoPath);
    return found == data_->readingSessions.end() ? nullptr : &found->second;
}

void Settings::set_reading_session(const std::string& repoPath, reading::ReadingSession session) {
    const auto* previous = get_reading_session(repoPath);
    if (previous && *previous == session) return;
    data_->readingSessions[repoPath] = std::move(session);
    save_if_auto();
}

review_files::DisplayMode Settings::get_review_display_mode(const std::string& repoPath) const {
    auto found = data_->reviewDisplayModes.find(repoPath);
    return found == data_->reviewDisplayModes.end() ? review_files::DisplayMode::SelectedFile : found->second;
}

void Settings::set_review_display_mode(const std::string& repoPath, review_files::DisplayMode mode) {
    if (get_review_display_mode(repoPath) == mode) return;
    data_->reviewDisplayModes[repoPath] = mode;
    save_if_auto();
}

const std::vector<std::string>& Settings::get_pinned_repos() const { return data_->pinnedRepos; }

void Settings::set_repo_pinned(const std::string& path, bool pinned) {
    if (path.empty()) return;
    if ((std::find(data_->pinnedRepos.begin(), data_->pinnedRepos.end(), path) != data_->pinnedRepos.end()) == pinned) return;
    std::erase(data_->pinnedRepos, path);
    if (pinned) data_->pinnedRepos.push_back(path);
    save_if_auto();
}

bool Settings::section_collapsed(const std::string& repo, const std::string& section) const {
    auto found = data_->collapsedSections.find(repo);
    return found != data_->collapsedSections.end() && found->second.contains(section);
}

void Settings::set_section_collapsed(const std::string& repo, const std::string& section, bool collapsed) {
    if (section_collapsed(repo, section) == collapsed) return;
    if (collapsed) data_->collapsedSections[repo].insert(section);
    else data_->collapsedSections[repo].erase(section);
    save_if_auto();
}

std::string Settings::repository_identity(const std::string& path) const {
    const auto found = data_->repositoryHeads.find(path);
    return found == data_->repositoryHeads.end() ? "" : found->second;
}

void Settings::remember_repository_identity(const std::string& path, const std::string& head) {
    if (path.empty() || !reading::is_object_id(head) || data_->repositoryHeads[path] == head) return;
    data_->repositoryHeads[path] = head;
    save_if_auto();
}

bool Settings::relink_repository(const std::string& oldPath, const std::string& newPath) {
    if (oldPath.empty() || newPath.empty() || oldPath == newPath) return false;
    if (data_->readingSessions.contains(newPath) || data_->codeBookmarks.contains(newPath) ||
        std::find(data_->openRepos.begin(), data_->openRepos.end(), newPath) != data_->openRepos.end()) return false;
    const auto previous = *data_;
    const auto previousPending = pendingSave_;
    for (auto* list : {&data_->openRepos, &data_->recentRepos, &data_->pinnedRepos})
        for (auto& path : *list) if (path == oldPath) path = newPath;
    if (data_->lastActiveRepo == oldPath) data_->lastActiveRepo = newPath;
    auto move = [&](auto& values) {
        auto node = values.extract(oldPath);
        if (!node.empty()) { node.key() = newPath; values.insert(std::move(node)); }
    };
    move(data_->repositoryHeads);
    move(data_->readingSessions);
    move(data_->codeBookmarks);
    move(data_->reviewDisplayModes);
    move(data_->collapsedSections);
    if (!auto_save_enabled) return true;
    write_save_file();
    if (!saveError.empty()) { *data_ = previous; pendingSave_ = previousPending; return false; }
    return true;
}
