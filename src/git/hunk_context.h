#pragma once

#include "content_reader.h"

namespace git {

struct ContextRange {
    int oldLine = 1;
    int newLine = 1;
    int count = 0;
};

inline ContextRange context_range(const ecs::FileDiff& file, size_t index, bool above, int count,
                               int previousBelow = 0) {
    const auto& hunk = file.hunks[index];
    if (above) {
        const int oldLimit = index ? file.hunks[index - 1].oldStart + file.hunks[index - 1].oldCount : 1;
        const int newLimit = index ? file.hunks[index - 1].newStart + file.hunks[index - 1].newCount : 1;
        count = std::max(0, std::min({count, hunk.oldStart - oldLimit - previousBelow,
                                     hunk.newStart - newLimit - previousBelow}));
        return {hunk.oldStart - count, hunk.newStart - count, count};
    }
    const int oldLine = hunk.oldStart + hunk.oldCount;
    const int newLine = hunk.newStart + hunk.newCount;
    if (index + 1 < file.hunks.size()) count = std::min({count, file.hunks[index + 1].oldStart - oldLine,
                                                       file.hunks[index + 1].newStart - newLine});
    return {oldLine, newLine, std::max(0, count)};
}


inline ecs::HunkContextResult read_hunk_context(FileRequest before, FileRequest after,
                                               ContextRange range, std::stop_token stop = {}) {
    ecs::HunkContextResult result;
    result.lines.oldStart = range.oldLine;
    result.lines.newStart = range.newLine;
    if (range.count <= 0) return result;
    before.page = {ecs::FilePageRequest::Action::TargetLine, {}, range.oldLine};
    after.page = {ecs::FilePageRequest::Action::TargetLine, {}, range.newLine};
    auto old = read_file(before, stop);
    auto next = read_file(after, stop);
    if (!old.error.empty() || !next.error.empty()) {
        result.error = !old.error.empty() ? old.error : next.error;
        return result;
    }
    if (old.diff.isBinary || next.diff.isBinary) { result.error = "Context is unavailable for binary files"; return result; }
    auto collect = [&](const ecs::FullFileContent& content, int first) {
        std::vector<std::string> lines;
        for (const auto& hunk : content.diff.hunks) {
            for (size_t i = 0; i < hunk.lines.size(); ++i) {
                int line = hunk.newStart + static_cast<int>(i);
                if (line < first || line >= first + range.count) continue;
                if ((line == content.page.begin.line && content.page.begin.continuation) ||
                    (line == content.page.next.line && content.page.next.continuation && content.page.next.offset < content.page.totalBytes)) {
                    result.error = "Context exceeds the bounded reader; open source to read more";
                    break;
                }
                lines.push_back(hunk.lines[i]);
            }
        }
        if (lines.size() < static_cast<size_t>(range.count) && content.page.next.offset < content.page.totalBytes)
            result.error = "Context exceeds the bounded reader; open source to read more";
        return lines;
    };
    auto oldLines = collect(old, range.oldLine);
    auto newLines = collect(next, range.newLine);
    if (oldLines != newLines) result.error = "Context changed; refresh this review before expanding";
    if (stop.stop_requested()) result.error = "Context load cancelled";
    if (!result.error.empty()) return result;
    for (const auto& line : newLines) result.bytes += line.size();
    if (result.bytes > 256 * 1024) { result.error = "Context exceeds 256 KiB; open source to read more"; result.bytes = 0; return result; }
    result.lines.lines = std::move(newLines);
    result.lines.oldCount = result.lines.newCount = static_cast<int>(result.lines.lines.size());
    if (!result.lines.lines.empty() && next.page.next.offset == next.page.totalBytes &&
        !next.diff.hunks.empty() && next.diff.hunks.back().noNewline.contains(next.diff.hunks.back().lines.size() - 1) &&
        range.newLine + result.lines.newCount == next.page.next.line + (next.page.next.continuation ? 1 : 0))
        result.lines.noNewline.insert(result.lines.lines.size() - 1);
    return result;
}

}
