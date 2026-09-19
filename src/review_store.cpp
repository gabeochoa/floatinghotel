#include "review_store.h"

#include <cctype>
#include <filesystem>
#include <functional>
#include <optional>

#include <nlohmann/json.hpp>

#include <afterhours/src/plugins/files.h>
#include <afterhours/src/logging.h>

#include "ecs/components.h"
#include "util/storage_key.h"

namespace review_store {

namespace {

nlohmann::json encode_comment(const ecs::ReviewComponent::Comment& c) {
    nlohmann::json ranges = nlohmann::json::array();
    for (const auto& range : c.ranges) ranges.push_back({{"first", range.first}, {"last", range.last}, {"old_side", range.oldSide}});
    return {{"ranges", ranges}, {"scope", c.scope}, {"file", c.file}, {"line", c.line},
        {"end_line", c.endLine}, {"old_side", c.oldSide}, {"resolved", c.resolved}, {"text", c.text},
        {"revision", c.revision}, {"code_context", c.codeContext}, {"kind", review_comment_kind_label(c.kind)}};
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
    comment.revision = value.value("revision", std::string{});
    comment.codeContext = value.value("code_context", std::string{});
    comment.kind = parse_review_comment_kind(value.value("kind", std::string{}));
    if (value.contains("ranges")) for (const auto& range : value.at("ranges")) {
        const int first = range.at("first").get<int>();
        const int last = range.at("last").get<int>();
        if (first <= 0 || last < first || comment.ranges.size() >= 512) throw std::runtime_error("Invalid feedback range");
        comment.ranges.push_back({first, last, range.value("old_side", false)});
    }
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
    return storage::key(repoPath);
}

}  // namespace

std::string review_path(const std::string& repoPath, const std::string& scope) {
    return (reviews_dir() / (repo_key(repoPath) + (scope.empty() ? "" : "-" + repo_key(scope)) + ".json")).string();
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

std::string copy_repository_reviews(const std::string& oldPath, const std::string& newPath) {
    std::vector<std::pair<std::string, std::string>> copies;
    std::error_code error;
    const auto prefix = repo_key(oldPath);
    const auto legacy = std::to_string(std::hash<std::string>{}(oldPath));
    for (const auto& entry : std::filesystem::directory_iterator(reviews_dir(), error)) {
        const auto name = entry.path().filename().string();
        if (entry.path().extension() != ".json" || (name != prefix + ".json" && !name.starts_with(prefix + "-") && name != legacy + ".json" && !name.starts_with(legacy + "-"))) continue;
        try {
            auto contents = afterhours::files::read_string(entry.path().string());
            if (!contents) return "Unable to read saved review";
            auto json = nlohmann::json::parse(*contents);
            if (json.value("repo_path", std::string{}) != oldPath || json.value("schema_version", 0) > 2)
                return "Saved review identity or version does not match";
            if ((name == legacy + ".json" || name.starts_with(legacy + "-")) &&
                std::filesystem::exists(review_path(oldPath, json.value("review_scope", std::string{})))) continue;
            json["repo_path"] = newPath;
            const auto destination = review_path(newPath, json.value("review_scope", std::string{}));
            const auto encoded = json.dump(2);
            if (std::filesystem::exists(destination)) {
                auto existing = afterhours::files::read_string(destination);
                if (!existing || nlohmann::json::parse(*existing) != json) return "The new location already has different saved reviews";
            } else copies.emplace_back(destination, encoded);
        } catch (const std::exception& failure) { return failure.what(); }
    }
    if (error) return error.message();
    for (const auto& [path, contents] : copies)
        if (!afterhours::files::write_string_atomic(path, contents)) return "Unable to copy saved review";
    return {};
}

bool save_review(const std::string& repoPath, const ecs::ReviewComponent& review) {
    if (repoPath.empty() || !review.loadError.empty()) return false;
    const auto path = review_path(repoPath, review.storageScope);
    const auto existing = afterhours::files::read_string(path);
    std::error_code readError;
    if (!existing && (std::filesystem::exists(path, readError) || readError)) return false;
    if (existing) {
        try {
            const auto identity = nlohmann::json::parse(*existing);
            if (identity.value("schema_version", 0) > 2 || identity.value("repo_path", repoPath) != repoPath ||
                identity.value("review_scope", std::string{}) != review.storageScope) return false;
        } catch (...) { return false; }
    }

    nlohmann::json j;
    j["schema_version"] = 2;
    j["repo_path"] = repoPath;  // for debuggability (filename is a hash)
    j["reviewing"] = review.reviewing;
    j["basket_open"] = review.basketOpen;
    j["review_scope"] = review.storageScope;

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
    j["editing_kind"] = review_comment_kind_label(review.editingCommentKind);

    j["approved_hunks"] = review.approvedHunks;  // set<string> -> array
    j["reviewed_files"] = review.reviewedFiles;
    j["verdicts"] = nlohmann::json::object();
    for (const auto& [scope, decision] : review.verdicts)
        j["verdicts"][scope] = {{"verdict", review_verdict_label(decision.verdict)}, {"signature", decision.signature}};
    j["queue"] = {{"position", review.queue.position}, {"completed", review.queue.completed}, {"commits", nlohmann::json::array()}};
    for (const auto& commit : review.queue.commits)
        j["queue"]["commits"].push_back({{"hash", commit.hash}, {"short_hash", commit.shortHash}, {"subject", commit.subject},
            {"author", commit.author}, {"author_date", commit.authorDate}, {"decorations", commit.decorations}, {"parents", commit.parentHashes}});
    j["folded_hunks"] = review.foldedHunks;
    j["seen_sig"] = review.seenSig;              // map<string,string> -> object
    j["baseline_head"] = review.baselineHead;
    j["baseline_diff_sig"] = review.baselineDiffSig;
    j["baseline_snapshot"] = review.baselineSnapshot;
    j["baseline_captured_at"] = review.baselineCapturedAt;

    if (!afterhours::files::write_string_atomic(path, j.dump(2))) {
        log_warn("Failed to save review to {}", path);
        return false;
    }
    return true;
}

bool persist_review(const std::string& repoPath, ecs::ReviewComponent& review) {
    if (!review.dirty) return true;
    if (!save_review(repoPath, review)) return false;
    review.dirty = false;
    return true;
}

void load_review(const std::string& repoPath, ecs::ReviewComponent& target) {
    if (repoPath.empty()) return;
    target.loadError.clear();
    ecs::ReviewComponent review;
    review.storageScope = target.storageScope;
    review.storageRepoPath = target.storageRepoPath;
    std::string path = review_path(repoPath, review.storageScope);
    const auto destination = path;
    std::error_code pathError;
    if (!std::filesystem::exists(path, pathError) && !pathError) {
        const auto legacy = std::to_string(std::hash<std::string>{}(repoPath));
        const auto suffix = review.storageScope.empty() ? "" : "-" + std::to_string(std::hash<std::string>{}(review.storageScope));
        const auto candidate = (reviews_dir() / (legacy + suffix + ".json")).string();
        if (std::filesystem::exists(candidate, pathError) && !pathError) path = candidate;
    }
    std::optional<std::string> contents = afterhours::files::read_string(path);
    if (!contents) {
        std::error_code error;
        if (std::filesystem::exists(path, error) || error) target.loadError = "Review file is unreadable: " + path;
        return;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(*contents);
        if (!j.is_object()) throw std::runtime_error("Review must be an object");
        if (j.value("schema_version", 0) > 2) throw std::runtime_error("Review was written by a newer version");
        if (j.value("repo_path", repoPath) != repoPath || j.value("review_scope", std::string{}) != review.storageScope)
            throw std::runtime_error("Review identity does not match this repository and scope");

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
        review.editingCommentKind = parse_review_comment_kind(j.value("editing_kind", std::string{}));
        if (review.editingComment < 0 || static_cast<size_t>(review.editingComment) >= review.comments.size()) {
            review.editingComment = -1;
            review.editingCommentText.clear();
        }
        review.dirty = false;

        review.approvedHunks =
            j.value("approved_hunks", std::set<std::string>{});
        review.reviewedFiles = j.value("reviewed_files", std::map<std::string, std::string>{});
        review.verdicts.clear();
        if (j.contains("verdicts"))
            for (auto it = j["verdicts"].begin(); it != j["verdicts"].end(); ++it)
                review.verdicts[it.key()] = {parse_review_verdict(it.value().value("verdict", std::string{})),
                    it.value().value("signature", std::string{})};
        review.queue = {};
        if (j.contains("queue")) {
            const auto& queue = j["queue"];
            review.queue.position = queue.value("position", size_t{0});
            review.queue.completed = queue.value("completed", std::set<std::string>{});
            if (queue.contains("commits"))
                for (const auto& value : queue["commits"]) {
                    auto hash = value.value("hash", std::string{});
                    if ((hash.size() != 40 && hash.size() != 64) || hash.find_first_not_of("0123456789abcdef") != std::string::npos) continue;
                    review.queue.commits.push_back({hash, value.value("short_hash", hash.substr(0, 7)), value.value("subject", std::string{}),
                        value.value("author", std::string{}), value.value("author_date", std::string{}),
                        value.value("decorations", std::string{}), value.value("parents", std::string{})});
                }
            review.queue = ecs::refreshed_review_queue(review.queue, review.queue.commits);
        }
        review.foldedHunks = j.value("folded_hunks", std::set<std::string>{});
        review.seenSig =
            j.value("seen_sig", std::map<std::string, std::string>{});
        review.baselineHead = j.value("baseline_head", std::string{});
        review.baselineDiffSig = j.value("baseline_diff_sig", std::string{});
        review.baselineSnapshot = j.value("baseline_snapshot", std::string{});
        review.baselineCapturedAt = j.value("baseline_captured_at", int64_t{0});

        target.reviewing = std::move(review.reviewing);
        target.basketOpen = std::move(review.basketOpen);
        target.comments = std::move(review.comments);
        target.drafts = std::move(review.drafts);
        target.composingKey = std::move(review.composingKey);
        target.composingText = std::move(review.composingText);
        target.composingFocus = std::move(review.composingFocus);
        target.composingFile = std::move(review.composingFile);
        target.composingScope = std::move(review.composingScope);
        target.composingLine = std::move(review.composingLine);
        target.composingEndLine = std::move(review.composingEndLine);
        target.composingRanges = std::move(review.composingRanges);
        target.composingOldSide = std::move(review.composingOldSide);
        target.composingRevision = std::move(review.composingRevision);
        target.composingCodeContext = std::move(review.composingCodeContext);
        target.composingKind = std::move(review.composingKind);
        target.editingComment = std::move(review.editingComment);
        target.editingCommentText = std::move(review.editingCommentText);
        target.editingCommentKind = std::move(review.editingCommentKind);
        target.dirty = std::move(review.dirty);
        target.approvedHunks = std::move(review.approvedHunks);
        target.reviewedFiles = std::move(review.reviewedFiles);
        target.verdicts = std::move(review.verdicts);
        target.queue = std::move(review.queue);
        target.foldedHunks = std::move(review.foldedHunks);
        target.seenSig = std::move(review.seenSig);
        target.baselineHead = std::move(review.baselineHead);
        target.baselineDiffSig = std::move(review.baselineDiffSig);
        target.baselineSnapshot = std::move(review.baselineSnapshot);
        target.baselineCapturedAt = std::move(review.baselineCapturedAt);
        if (path != destination && !save_review(repoPath, target)) target.loadError = "Legacy review loaded, but migration could not be saved";
        log_info("Review loaded from {}", path);
    } catch (const std::exception& e) {
        target.loadError = std::string("Review could not be loaded: ") + e.what();
        log_warn("Failed to parse review file {}: {} (ignoring)", path, e.what());
    }
}

bool switch_review_scope(const std::string& repoPath, const std::string& scope,
                         ecs::ReviewComponent& review, bool persist) {
    if (review.storageScope == scope && review.storageRepoPath == repoPath) return true;
    auto oldRepo = review.storageRepoPath.empty() ? repoPath : review.storageRepoPath;
    if (persist && !persist_review(oldRepo, review)) return false;
    const bool reading = review.reviewing;
    ecs::reset_review(review);
    review.storageRepoPath = repoPath;
    review.storageScope = scope;
    if (persist) load_review(repoPath, review);
    review.reviewing = reading;
    return true;
}

}  // namespace review_store
