#include "review_store.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>

#include <nlohmann/json.hpp>

#include <afterhours/src/plugins/files.h>
#include <afterhours/src/logging.h>

#include "ecs/components.h"

namespace review_store {

namespace {

// Atomic replace: write to a sibling temp file then rename over the target, so
// a crash mid-write can't truncate an existing review. (Same routine proposed
// for afterhours::files in docs/afterhours-persistence-proposal.md — once that
// lands, delete this and call the shared helper.)
bool write_atomic(const std::filesystem::path& path, const std::string& content) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.good()) return false;
        f << content;
        f.flush();
        if (!f.good()) { std::filesystem::remove(tmp, ec); return false; }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) { std::filesystem::remove(tmp, ec); return false; }
    return true;
}

// The reviews/ dir under the afterhours save path (created on demand).
std::filesystem::path reviews_dir() {
    std::filesystem::path dir = afterhours::files::get_save_path();
    if (dir.empty()) dir = std::filesystem::current_path();
    dir /= "reviews";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// Filesystem-safe per-repo key (repo paths contain slashes).
std::string repo_key(const std::string& repoPath) {
    return std::to_string(std::hash<std::string>{}(repoPath));
}

}  // namespace

std::string review_path(const std::string& repoPath) {
    return (reviews_dir() / (repo_key(repoPath) + ".json")).string();
}

std::string markdown_path(const std::string& repoPath,
                          const std::string& branch) {
    std::string b;
    for (char c : branch)
        b += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
                 ? c : '_';
    if (b.empty()) b = "HEAD";
    return (reviews_dir() / (repo_key(repoPath) + "-" + b + ".md")).string();
}

void save_review(const std::string& repoPath, const ecs::ReviewComponent& review) {
    if (repoPath.empty()) return;

    nlohmann::json j;
    j["repo_path"] = repoPath;  // for debuggability (filename is a hash)
    j["reviewing"] = review.reviewing;
    j["basket_open"] = review.basketOpen;

    nlohmann::json comments = nlohmann::json::array();
    for (const auto& c : review.comments) {
        comments.push_back({{"scope", c.scope},
                            {"file", c.file},
                            {"line", c.line},
                            {"text", c.text}});
    }
    j["comments"] = std::move(comments);

    j["approved_hunks"] = review.approvedHunks;  // set<string> -> array
    j["folded_hunks"] = review.foldedHunks;
    j["seen_sig"] = review.seenSig;              // map<string,string> -> object
    j["baseline_head"] = review.baselineHead;
    j["baseline_diff_sig"] = review.baselineDiffSig;

    std::string path = review_path(repoPath);
    if (!write_atomic(path, j.dump(2)))
        log_warn("Failed to save review to {}", path);
}

void load_review(const std::string& repoPath, ecs::ReviewComponent& review) {
    if (repoPath.empty()) return;
    std::string path = review_path(repoPath);
    if (!std::filesystem::exists(path)) return;

    try {
        std::ifstream f(path);
        nlohmann::json j = nlohmann::json::parse(f);

        review.reviewing = j.value("reviewing", false);
        review.basketOpen = j.value("basket_open", true);

        review.comments.clear();
        if (j.contains("comments")) {
            for (const auto& c : j["comments"]) {
                ecs::ReviewComponent::Comment cm;
                cm.scope = c.value("scope", std::string{});
                cm.file = c.value("file", std::string{});
                cm.line = c.value("line", 0);
                cm.text = c.value("text", std::string{});
                review.comments.push_back(std::move(cm));
            }
        }

        review.approvedHunks =
            j.value("approved_hunks", std::set<std::string>{});
        review.foldedHunks = j.value("folded_hunks", std::set<std::string>{});
        review.seenSig =
            j.value("seen_sig", std::map<std::string, std::string>{});
        review.baselineHead = j.value("baseline_head", std::string{});
        review.baselineDiffSig = j.value("baseline_diff_sig", std::string{});

        log_info("Review loaded from {}", path);
    } catch (const std::exception& e) {
        log_warn("Failed to parse review file {}: {} (ignoring)", path, e.what());
    }
}

}  // namespace review_store
