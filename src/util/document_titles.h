#pragma once

#include "reading_workspace.h"
#include <filesystem>

namespace reading {

struct DocumentTitle {
    std::string label;
    std::string badge;
    std::string tooltip;
};

inline std::vector<DocumentTitle> document_titles(const std::vector<Document>& documents) {
    // Short hash only: never grow past 7 characters to chase uniqueness —
    // the tooltip carries the full identity when it matters.
    auto compact = [&](const std::string& revision) {
        if (revision.empty()) return std::string("Working tree");
        if (revision == "INDEX") return std::string("Index");
        if (!is_object_id(revision)) return revision;
        return revision.substr(0, 7);
    };
    auto parent_suffix = [](const std::string& path, size_t count) {
        auto parent = std::filesystem::path(path).parent_path();
        std::filesystem::path suffix;
        while (!parent.empty() && count-- > 0) {
            suffix = parent.filename() / suffix;
            parent = parent.parent_path();
        }
        auto text = suffix.generic_string();
        if (!text.empty() && text.back() == '/') text.pop_back();
        return text.empty() ? std::string(".") : text;
    };
    std::vector<DocumentTitle> result;
    result.reserve(documents.size());
    for (const auto& document : documents) {
        DocumentTitle title;
        if (const auto* source = std::get_if<SourceLocation>(&document.location)) {
            const auto& path = source->destination.path;
            const auto& revision = revision_text(source->destination.revision);
            title.label = std::filesystem::path(path).filename().string();
            std::vector<std::string> duplicates;
            bool otherRevision = false;
            for (const auto& other : documents) {
                const auto* file = std::get_if<SourceLocation>(&other.location);
                if (!file) continue;
                if (file->destination.path == path) {
                    otherRevision |= file->destination.revision != source->destination.revision;
                } else if (std::filesystem::path(file->destination.path).filename().string() == title.label)
                    duplicates.push_back(file->destination.path);
            }
            if (!duplicates.empty()) {
                size_t count = 1;
                auto suffix = parent_suffix(path, count);
                while (std::any_of(duplicates.begin(), duplicates.end(), [&](const auto& other) {
                    return parent_suffix(other, count) == suffix;
                })) suffix = parent_suffix(path, ++count);
                title.label += " · " + suffix;
            }
            // A version badge only earns its place when another version
            // of the same file is open to confuse it with; a lone pinned
            // file shows no hash at all.
            if (otherRevision) title.badge = compact(revision);
            title.tooltip = path + " @ " + (revision.empty() ? "working tree" : revision);
            if (document.editDirty) {
                title.label += " •";
                title.tooltip += "\nUnsaved changes";
            }
        } else std::visit([&](const auto& review) {
            using T = std::decay_t<decltype(review)>;
            if constexpr (std::is_same_v<T, WorkingChanges>) {
                title.label = review.staged ? "Staged changes" : "Unstaged changes";
                title.tooltip = title.label;
            } else if constexpr (std::is_same_v<T, CommitReview>) {
                const auto& revision = revision_text(review.commit);
                title.label = document.subject.empty() ? "Commit " + compact(revision) : document.subject;
                bool duplicate = false;
                bool otherParent = false;
                for (const auto& other : documents) {
                    if (other.id == document.id) continue;
                    const auto* location = std::get_if<ReviewLocation>(&other.location);
                    const auto* commit = location ? std::get_if<CommitReview>(&location->destination) : nullptr;
                    if (commit && (other.subject == document.subject || commit->commit == review.commit)) duplicate = true;
                    if (commit && commit->commit == review.commit && commit->parent != review.parent) otherParent = true;
                }
                if (duplicate) title.badge = compact(revision);
                if (review.parent) title.badge += (title.badge.empty() ? "" : " · ") + std::string("from ") + compact(revision_text(*review.parent));
                else if (otherParent) title.badge += " · first parent";
                title.tooltip = title.label + "\nCommit " + revision + "\n" +
                    (review.parent ? "Parent " + revision_text(*review.parent) : "First parent");
            } else {
                title.label = document.subject.empty() ? compact(revision_text(review.before)) + " → " + compact(revision_text(review.after)) : document.subject;
                title.tooltip = "Compare " + revision_text(review.before) + " → " + revision_text(review.after);
            }
        }, std::get<ReviewLocation>(document.location).destination);
        result.push_back(std::move(title));
    }
    return result;
}

}
