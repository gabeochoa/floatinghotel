#pragma once

#include "review_anchor.h"
#include <map>
#include <tuple>

namespace follow_up_review {

struct Item {
    std::string path;
    int line = 0;
    int endLine = 0;
    bool oldSide = false;
    bool changed = false;
    size_t comments = 0;
    review_anchor::Status status = review_anchor::Status::Unknown;
};

inline std::vector<Item> build(const std::vector<ecs::FileDiff>& changed,
    const std::vector<ecs::ReviewComponent::Comment>& comments, const std::vector<ecs::FileDiff>& current) {
    std::map<std::tuple<std::string, int, int, bool>, Item> entries;
    for (const auto& comment : comments) {
        if (comment.resolved || comment.scope != "wt") continue;
        const auto anchor = review_anchor::locate(comment, &current);
        std::string path = comment.file;
        for (const auto& file : current)
            if (file.filePath == path || (comment.oldSide && file.oldPath == path)) { path = file.filePath; break; }
        const int line = anchor.line > 0 ? anchor.line : comment.line;
        const int end = line + std::max(0, comment.endLine - comment.line);
        auto [found, inserted] = entries.try_emplace({path, line, end, comment.oldSide},
            Item{path, line, end, comment.oldSide, false, 0, anchor.status});
        ++found->second.comments;
        if (!inserted && found->second.status != anchor.status) found->second.status = review_anchor::Status::Ambiguous;
    }
    for (const auto& file : changed) {
        auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& value) { return value.second.path == file.filePath; });
        if (found != entries.end()) found->second.changed = true;
        else entries.emplace(std::tuple{file.filePath, 0, 0, false}, Item{file.filePath, 0, 0, false, true});
    }
    std::vector<Item> result;
    result.reserve(entries.size());
    for (auto& [key, item] : entries) result.push_back(std::move(item));
    return result;
}

inline std::string label(const Item& item) {
    std::string text = item.comments ? "Unresolved" : "Changed";
    if (item.comments) switch (item.status) {
        case review_anchor::Status::Outdated: text = "Outdated"; break;
        case review_anchor::Status::Ambiguous: text = "Ambiguous"; break;
        case review_anchor::Status::Unknown: text = "Unlocated"; break;
        case review_anchor::Status::Relocated: text = "Moved"; break;
        case review_anchor::Status::Current: break;
    }
    text += " · " + item.path;
    if (item.line > 0) text += ":" + std::to_string(item.line);
    if (item.endLine > item.line) text += "–" + std::to_string(item.endLine);
    if (item.oldSide) text += " (old)";
    if (item.changed && item.comments) text += " · changed";
    if (item.comments > 1) text += " · " + std::to_string(item.comments) + " comments";
    return text;
}

}
