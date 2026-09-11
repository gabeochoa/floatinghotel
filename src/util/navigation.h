#pragma once

#include "../ecs/components.h"
#include <optional>

namespace navigation {

inline ecs::NavigationLocation location(const ecs::RepoComponent& repo, bool reviewing = false) {
    using Kind = ecs::NavigationLocation::Kind;
    ecs::NavigationLocation next;
    if (!repo.fullFilePath.empty()) next = {Kind::FullFile, repo.fullFilePath, repo.fullFileRevision};
    else if (!repo.selectedCommitHash.empty()) next = {Kind::Commit, "", repo.selectedCommitHash};
    else if (!repo.selectedFilePath.empty() && (!reviewing || repo.selectedFileStaged))
        next = {Kind::File, repo.selectedFilePath, "", repo.selectedFileStaged};
    next.reviewing = reviewing;
    return next;
}

inline void record(ecs::NavigationHistory& history, const ecs::NavigationLocation& next) {
    if (!history.entries.empty() && history.entries[history.index] == next) return;
    if (!history.entries.empty()) history.entries.resize(history.index + 1);
    history.entries.push_back(next);
    history.index = history.entries.size() - 1;
}

inline std::optional<ecs::NavigationLocation> step(ecs::NavigationHistory& history, int direction) {
    if (history.entries.empty() || direction == 0 || (direction < 0 && history.index == 0) ||
        (direction > 0 && history.index + 1 >= history.entries.size())) return std::nullopt;
    if (direction < 0) --history.index;
    else ++history.index;
    return history.entries[history.index];
}

}
