#include "review_store.h"

#include <cctype>
#include <filesystem>
#include <functional>
#include <optional>

#include <nlohmann/json.hpp>

#include <afterhours/src/plugins/files.h>
#include <afterhours/src/logging.h>

#include "ecs/components.h"

namespace review_store {

namespace {

nlohmann::json encode_comment(const ecs::ReviewComponent::Comment& c) {
    return {{"scope", c.scope}, {"file", c.file}, {"line", c.line},
        {"end_line", c.endLine}, {"old_side", c.oldSide}, {"resolved", c.resolved}, {"text", c.text}};
}

ecs::ReviewComponent::Comment decode_comment(const nlohmann::json& value) {
    ecs::ReviewComponent::Comment comment;
    comment.scope = value.value("scope", std::string{});
    comment.file = value.value("file", std::string{});
    comment.line = value.value("line", 0);
    comment.endLine = value.value("end_line", comment.line);
    comment.oldSide = value.value("old_side", false);
    comment.resolved = value.value("resolved", false);
    comment.text = value.value("text", std::string{});
    return comment;
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
    for (const auto& c : review.comments) comments.push_back(encode_comment(c));
    j["comments"] = std::move(comments);
    auto drafts = review.drafts;
    if (!review.composingKey.empty()) {
        if (review.composingText.empty()) drafts.erase(review.composingKey);
        else drafts[review.composingKey] = ecs::pending_comment(review);
    }
    j["drafts"] = nlohmann::json::object();
    for (const auto& [key, draft] : drafts) j["drafts"][key] = encode_comment(draft);
    j["active_draft"] = review.composingKey;
    j["editing_comment"] = review.editingComment;
    j["editing_text"] = review.editingCommentText;

    j["approved_hunks"] = review.approvedHunks;  // set<string> -> array
    j["folded_hunks"] = review.foldedHunks;
    j["seen_sig"] = review.seenSig;              // map<string,string> -> object
    j["baseline_head"] = review.baselineHead;
    j["baseline_diff_sig"] = review.baselineDiffSig;

    std::string path = review_path(repoPath);
    if (!afterhours::files::write_string_atomic(path, j.dump(2)))
        log_warn("Failed to save review to {}", path);
}

void load_review(const std::string& repoPath, ecs::ReviewComponent& review) {
    if (repoPath.empty()) return;
    std::string path = review_path(repoPath);
    std::optional<std::string> contents = afterhours::files::read_string(path);
    if (!contents) return;  // missing/unreadable -> fresh review

    try {
        nlohmann::json j = nlohmann::json::parse(*contents);

        review.reviewing = j.value("reviewing", false);
        review.basketOpen = j.value("basket_open", true);

        review.comments.clear();
        if (j.contains("comments")) {
            for (const auto& c : j["comments"]) {
                review.comments.push_back(decode_comment(c));
            }
        }
        review.drafts.clear();
        review.composingKey.clear();
        review.composingText.clear();
        if (j.contains("drafts")) {
            for (auto it = j["drafts"].begin(); it != j["drafts"].end(); ++it)
                review.drafts[it.key()] = decode_comment(it.value());
        }
        auto activeDraft = j.value("active_draft", std::string{});
        if (review.drafts.contains(activeDraft)) ecs::begin_comment(review, activeDraft, review.drafts.at(activeDraft));
        review.editingComment = j.value("editing_comment", -1);
        review.editingCommentText = j.value("editing_text", std::string{});
        if (review.editingComment < 0 || static_cast<size_t>(review.editingComment) >= review.comments.size()) {
            review.editingComment = -1;
            review.editingCommentText.clear();
        }
        review.dirty = false;

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
