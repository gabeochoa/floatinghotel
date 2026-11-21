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
    bool vimMode = false;
    bool foldNoiseFiles = true;
    std::string theme = "dark";
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
    std::string gitPath;
    std::string acpAgent = "muse";
    std::string acpMcpServers = "[]";  // raw JSON text, round-tripped verbatim
    std::vector<std::string> recentRepos;
    std::vector<std::string> pinnedRepos;
    std::map<std::string, std::string> repositoryHeads;
    std::map<std::string, int> commandUsage;
    std::vector<std::string> commandRecent;
    std::vector<std::string> commandQueries;
    std::map<std::string, std::set<std::string>> collapsedSections;
    std::map<std::string, std::vector<CodeBookmark>> codeBookmarks;
    std::map<std::string, reading::ReadingSession> readingSessions;
    std::map<std::string, review_files::DisplayMode> reviewDisplayModes;
    std::map<std::string, fold_defaults::Rules> foldRules;
};

Settings::Settings() { data_ = new Data(); }
Settings::~Settings() { delete data_; }

static float bounded_code_font_size(float size) {
    if (!std::isfinite(size)) return Settings::kDefaultCodeFontSize;
    // Code text stays on the UI type scale: snap to the nearest tier
    // (12/14/16, ties go up) instead of drifting at arbitrary sizes.
    float best = Settings::kDefaultCodeFontSize;
    for (float tier : {12.f, 14.f, 16.f})
        if (std::abs(size - tier) < std::abs(size - best) ||
            (std::abs(size - tier) == std::abs(size - best) && tier > best))
            best = tier;
    return best;
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
    if (!writeFuture_.valid() && saveState_ == Save::Pending) saveState_ = Save::Clean;
    std::string path = get_settings_path();
    loadError.clear();
    std::error_code fileError;
    if (!std::filesystem::exists(path, fileError) && !fileError) {
        if (!writeFuture_.valid() && saveState_ == Save::Blocked) saveState_ = Save::Clean;
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
        data_->commandUsage = j.value("command_usage", std::map<std::string, int>{});
        data_->commandRecent = j.value("command_recent", std::vector<std::string>{});
        data_->commandQueries = j.value("command_queries", std::vector<std::string>{});
        data_->pinnedRepos = j.value("pinned_repos", std::vector<std::string>{});
        data_->collapsedSections = j.value("collapsed_sections", std::map<std::string, std::set<std::string>>{});

        data_->windowHeight = std::clamp(j.value("window_height", 800), 200, 16384);
        data_->windowX = j.value("window_x", 100);
        data_->windowY = j.value("window_y", 100);
        data_->sidebarWidth = j.value("sidebar_width", 280.0f);
        if (!std::isfinite(data_->sidebarWidth)) data_->sidebarWidth = 280.f;
        data_->sidebarWidth = std::clamp(data_->sidebarWidth, 200.f, 16384.f);
        data_->windowCollapsed = j.value("window_shelf_collapsed", true);
        data_->vimMode = j.value("vim_mode", false);
        data_->foldNoiseFiles = j.value("fold_noise_files", true);
        data_->theme = j.value("theme", std::string("dark"));
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
        data_->gitPath = j.value("git_path", std::string{});
        data_->acpAgent = j.value("acp_agent", std::string{"muse"});
        if (j.contains("acp_mcp_servers")) data_->acpMcpServers = j["acp_mcp_servers"].dump();
        data_->recentRepos =
            j.value("recent_repos", std::vector<std::string>{});
        data_->reviewDisplayModes.clear();
        if (j.contains("review_display_modes") && j["review_display_modes"].is_object())
            for (const auto& [repo, value] : j["review_display_modes"].items())
                if (value == "all" || value == "selected") data_->reviewDisplayModes[repo] =
                    value == "all" ? review_files::DisplayMode::AllFiles : review_files::DisplayMode::SelectedFile;
        data_->foldRules.clear();
        if (j.contains("fold_rules") && j["fold_rules"].is_object())
            for (const auto& [repo, value] : j["fold_rules"].items()) {
                if (!value.is_object()) continue;
                fold_defaults::Rules rules;
                rules.patterns = value.value("patterns", std::vector<std::string>{});
                rules.minChangedLines = std::max(0, value.value("min_lines", 0));
                data_->foldRules[repo] = std::move(rules);
            }
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

        if (!writeFuture_.valid() && saveState_ == Save::Blocked) saveState_ = Save::Clean;
        log_info("Settings loaded from {}", path);
        return true;
    } catch (const std::exception& e) {
        *data_ = std::move(previous);
        loadError = std::string("Settings could not be loaded: ") + e.what();
        if (!writeFuture_.valid()) saveState_ = Save::Blocked;
        log_warn("Failed to parse settings file {}: {} (preserving existing settings)",
                 path, e.what());
        return false;
    }
}

std::string Settings::build_settings_json() const {
    nlohmann::json j;
    j["schema_version"] = 1;
    j["repository_heads"] = data_->repositoryHeads;
    j["command_usage"] = data_->commandUsage;
    j["command_recent"] = data_->commandRecent;
    j["command_queries"] = data_->commandQueries;
    j["pinned_repos"] = data_->pinnedRepos;
    j["collapsed_sections"] = data_->collapsedSections;
    j["window_width"] = data_->windowWidth;
    j["window_height"] = data_->windowHeight;
    j["window_shelf_collapsed"] = data_->windowCollapsed;
    j["vim_mode"] = data_->vimMode;
    j["fold_noise_files"] = data_->foldNoiseFiles;
    j["theme"] = data_->theme;
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
    j["git_path"] = data_->gitPath;
    j["acp_agent"] = data_->acpAgent;
    {
        auto servers = nlohmann::json::parse(data_->acpMcpServers, nullptr, false);
        j["acp_mcp_servers"] = servers.is_array() ? servers : nlohmann::json::array();
    }
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
    j["fold_rules"] = nlohmann::json::object();
    for (const auto& [repo, rules] : data_->foldRules)
        j["fold_rules"][repo] = {{"patterns", rules.patterns}, {"min_lines", rules.minChangedLines}};
    j["reading_sessions"] = nlohmann::json::object();
    for (const auto& [repo, session] : data_->readingSessions)
        j["reading_sessions"][repo] = reading::encode_session(session);

    return j.dump(2);
}

void Settings::apply_write_result(Save outcome, std::string error) {
    if (outcome == Save::Clean) {
        ++saveWriteCount_;
        saveError.clear();
        saveState_ = pendingSave_ ? Save::Pending : Save::Clean;
        return;
    }
    saveError = std::move(error);
    saveState_ = outcome;
    log_error("{}", saveError);
}

void Settings::write_save_file() {
    // A worker write may still be in flight; finish it first so writes
    // land in order.
    if (writeFuture_.valid()) {
        const auto error = writeFuture_.get();
        writeFuture_ = {};
        if (error.empty()) apply_write_result(Save::Clean, {});
        else apply_write_result(Save::Failed, "Failed to save settings: " + error);
    }
    pendingSave_.reset();
    if (!loadError.empty()) {
        apply_write_result(Save::Blocked, "Settings file was not overwritten: " + loadError);
        return;
    }
    std::string path = get_settings_path();
    if (!afterhours::files::write_string_atomic(path, build_settings_json())) {
        apply_write_result(Save::Failed, "Failed to save settings: " + path);
        return;
    }
    apply_write_result(Save::Clean, {});
    log_info("Settings saved to {}", path);
}

void Settings::save_if_auto() {
    if (!auto_save_enabled) return;
    pendingSave_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    if (!writeFuture_.valid()) saveState_ = Save::Pending;
}

void Settings::flush_pending_save(std::chrono::steady_clock::time_point now) {
    // Apply a finished worker write first (count / error surface here).
    if (writeFuture_.valid() &&
        writeFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const auto error = writeFuture_.get();
        writeFuture_ = {};
        if (error.empty()) {
            apply_write_result(Save::Clean, {});
            log_info("Settings saved");
        } else {
            apply_write_result(Save::Failed, "Failed to save settings: " + error);
        }
    }
    if (!auto_save_enabled || !pendingSave_ || now < *pendingSave_ || writeFuture_.valid()) return;
    pendingSave_.reset();
    if (!loadError.empty()) {
        apply_write_result(Save::Blocked, "Settings file was not overwritten: " + loadError);
        return;
    }
    // Serialise on this thread; only the file write goes to a worker.
    auto text = build_settings_json();
    auto path = get_settings_path();
    writeFuture_ = async_work::launch(
        [path, text = std::move(text)](std::stop_token) {
            return afterhours::files::write_string_atomic(path, text) ? std::string{} : path;
        },
        async_work::Priority::Background, std::string{"Background queue is full"});
    saveState_ = Save::Writing;
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

bool Settings::get_vim_mode() const { return data_->vimMode; }

void Settings::set_vim_mode(bool enabled) {
    if (data_->vimMode == enabled) return;
    data_->vimMode = enabled;
    save_if_auto();
}

const std::string& Settings::get_theme() const { return data_->theme; }

void Settings::set_theme(const std::string& name) {
    if (data_->theme == name) return;
    data_->theme = name;
    save_if_auto();
}
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

const std::map<std::string, int>& Settings::get_command_usage() const { return data_->commandUsage; }
const std::vector<std::string>& Settings::get_command_recent() const { return data_->commandRecent; }
const std::vector<std::string>& Settings::get_command_queries() const { return data_->commandQueries; }

void Settings::record_command_use(const std::string& label, const std::string& query) {
    if (label.empty()) return;
    ++data_->commandUsage[label];
    std::erase(data_->commandRecent, label);
    data_->commandRecent.insert(data_->commandRecent.begin(), label);
    if (data_->commandRecent.size() > 50) data_->commandRecent.resize(50);
    if (!query.empty()) {
        std::erase(data_->commandQueries, query);
        data_->commandQueries.insert(data_->commandQueries.begin(), query);
        if (data_->commandQueries.size() > 10) data_->commandQueries.resize(10);
    }
    save_if_auto();
}

const std::string& Settings::get_git_path() const { return data_->gitPath; }

void Settings::set_git_path(const std::string& path) {
    if (data_->gitPath == path) return;
    data_->gitPath = path;
    save_if_auto();
}

const std::string& Settings::get_acp_agent() const { return data_->acpAgent; }

void Settings::set_acp_agent(const std::string& id) {
    if (data_->acpAgent == id) return;
    data_->acpAgent = id;
    save_if_auto();
}

nlohmann::json Settings::get_acp_mcp_servers() const {
    auto servers = nlohmann::json::parse(data_->acpMcpServers, nullptr, false);
    return servers.is_array() ? servers : nlohmann::json::array();
}

void Settings::set_acp_mcp_servers(const nlohmann::json& servers) {
    const std::string text = servers.is_array() ? servers.dump() : "[]";
    if (data_->acpMcpServers == text) return;
    data_->acpMcpServers = text;
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
    // Keep max 50; the welcome list scrolls (virtual list).
    if (data_->recentRepos.size() > 50) {
        data_->recentRepos.resize(50);
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

fold_defaults::Rules Settings::get_fold_rules(const std::string& repoPath) const {
    auto found = data_->foldRules.find(repoPath);
    return found == data_->foldRules.end() ? fold_defaults::default_rules() : found->second;
}

bool Settings::get_fold_noise_files() const { return data_->foldNoiseFiles; }

void Settings::set_fold_noise_files(bool enabled) {
    if (data_->foldNoiseFiles == enabled) return;
    data_->foldNoiseFiles = enabled;
    save_if_auto();
}

void Settings::set_fold_rules(const std::string& repoPath, fold_defaults::Rules rules) {
    if (repoPath.empty()) return;
    rules.minChangedLines = std::max(0, rules.minChangedLines);
    auto found = data_->foldRules.find(repoPath);
    if (found != data_->foldRules.end() && found->second.patterns == rules.patterns &&
        found->second.minChangedLines == rules.minChangedLines) return;
    data_->foldRules[repoPath] = std::move(rules);
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
    move(data_->foldRules);
    if (!auto_save_enabled) return true;
    write_save_file();
    if (!saveError.empty()) { *data_ = previous; pendingSave_ = previousPending; return false; }
    return true;
}
