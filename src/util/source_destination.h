#pragma once

#include "../ecs/components.h"
#include "reading_anchor.h"

namespace reading {

inline std::string_view diff_text_at(const ecs::FileDiff& file, int line, DiffSide side) {
    for (const auto& hunk : file.hunks) {
        int oldLine = hunk.oldStart, newLine = hunk.newStart;
        for (const auto& text : hunk.lines) {
            const char sign = text.empty() ? ' ' : text.front();
            const bool present = side == DiffSide::Before ? sign != '+' : sign != '-';
            if (present && (side == DiffSide::Before ? oldLine : newLine) == line)
                return std::string_view(text).substr(std::min(size_t{1}, text.size()));
            if (sign != '+') ++oldLine;
            if (sign != '-') ++newLine;
        }
    }
    return {};
}

inline SourceLocation source_at_diff(const ReviewLocation& origin, const ecs::FileDiff& file,
                                     std::optional<ReadingAnchor> point = {}) {
    if (!point) {
        for (const auto& hunk : file.hunks) {
            int oldLine = hunk.oldStart, newLine = hunk.newStart;
            for (const auto& text : hunk.lines) {
                const char sign = text.empty() ? ' ' : text.front();
                if (sign == '+' || sign == '-') {
                    point = ReadingAnchor{file.filePath, scope(origin), sign == '-' ? DiffSide::Before : DiffSide::After,
                        sign == '-' ? oldLine : newLine, 1, 0.f, sign};
                    break;
                }
                ++oldLine;
                ++newLine;
            }
            if (point) break;
        }
    }
    const bool before = point ? point->side == DiffSide::Before : file.isDeleted;
    auto [oldRevision, newRevision] = diff_revisions(scope(origin));
    auto location = source(before && !file.oldPath.empty() ? file.oldPath : file.filePath,
        before ? std::move(oldRevision) : std::move(newRevision), point ? point->line : 0, origin);
    if (point) location.column = point->column;
    return location;
}

}
