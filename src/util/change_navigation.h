#pragma once

#include "../ecs/components.h"

namespace reading {

struct ChangeLocation {
    size_t file = 0;
    std::optional<size_t> hunk;
    ReadingAnchor anchor;
};

inline std::vector<ChangeLocation> change_locations(const std::vector<ecs::FileDiff>& files,
                                                   const std::vector<size_t>& order,
                                                   const std::string& revision) {
    std::vector<ChangeLocation> result;
    for (size_t fi : order) {
        const auto& file = files[fi];
        if (file.hunks.empty()) result.push_back({fi, {}, {file.filePath, revision, DiffSide::After, 1, 1, .1f}});
        for (size_t hi = 0; hi < file.hunks.size(); ++hi) {
            const auto& hunk = file.hunks[hi];
            std::optional<ReadingAnchor> added, removed;
            int oldLine = hunk.oldStart, newLine = hunk.newStart;
            for (const auto& line : hunk.lines) {
                if (line.empty()) continue;
                if (line[0] == '+' && !added) added = ReadingAnchor{file.filePath, revision, DiffSide::After, newLine, 1, .1f, '+'};
                if (line[0] == '-' && !removed) removed = ReadingAnchor{file.filePath, revision, DiffSide::Before, oldLine, 1, .1f, '-'};
                if (line[0] != '+') ++oldLine;
                if (line[0] != '-') ++newLine;
            }
            if (added || removed) result.push_back({fi, hi, added ? *added : *removed});
        }
    }
    return result;
}

inline std::optional<size_t> adjacent_change(const std::vector<ChangeLocation>& changes,
                                             const std::vector<ecs::FileDiff>& files,
                                             const std::optional<ReadingAnchor>& anchor,
                                             const std::string& selectedFile, int direction) {
    if (changes.empty()) return {};
    const auto& path = anchor ? anchor->path : selectedFile;
    auto first = std::find_if(changes.begin(), changes.end(), [&](const auto& change) { return change.anchor.path == path; });
    if (first == changes.end()) return direction > 0 ? 0 : changes.size() - 1;
    size_t index = static_cast<size_t>(first - changes.begin());
    if (anchor) {
        for (; index < changes.size() && changes[index].anchor.path == path; ++index) {
            const auto& change = changes[index];
            if (!change.hunk) break;
            const auto& hunk = files[change.file].hunks[*change.hunk];
            const bool before = anchor->side == DiffSide::Before;
            const int start = before ? hunk.oldStart : hunk.newStart;
            const int count = before ? hunk.oldCount : hunk.newCount;
            if (anchor->line < start) return direction > 0 ? std::optional{index} :
                index > 0 ? std::optional{index - 1} : std::nullopt;
            if (anchor->line - start < std::max(1, count)) break;
        }
        if (index == changes.size() || changes[index].anchor.path != path)
            return direction > 0 ? (index < changes.size() ? std::optional{index} : std::nullopt) : std::optional{index - 1};
    }
    if (direction > 0 && index + 1 < changes.size()) return index + 1;
    if (direction < 0 && index > 0) return index - 1;
    return {};
}

}
