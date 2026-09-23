#pragma once

#include "../ecs/components.h"

namespace diff_reconcile {

inline bool hunk_equal(const ecs::DiffHunk& a, const ecs::DiffHunk& b) {
    return a.oldStart == b.oldStart && a.oldCount == b.oldCount && a.newStart == b.newStart &&
           a.newCount == b.newCount && a.header == b.header && a.lines == b.lines &&
           a.noNewline == b.noNewline && a.movedLines == b.movedLines;
}

inline bool file_equal(const ecs::FileDiff& a, const ecs::FileDiff& b) {
    if (a.filePath != b.filePath || a.oldPath != b.oldPath || a.additions != b.additions ||
        a.deletions != b.deletions || a.isNew != b.isNew || a.isDeleted != b.isDeleted ||
        a.isRenamed != b.isRenamed || a.isBinary != b.isBinary || a.isFullContent != b.isFullContent ||
        a.isSubmodule != b.isSubmodule || a.oldMode != b.oldMode || a.newMode != b.newMode ||
        a.oldObject != b.oldObject || a.newObject != b.newObject ||
        a.isPartialContent != b.isPartialContent || a.hunks.size() != b.hunks.size())
        return false;
    for (size_t i = 0; i < a.hunks.size(); ++i)
        if (!hunk_equal(a.hunks[i], b.hunks[i])) return false;
    return true;
}

// Reuse the previous object for every file (and, inside a changed file, every
// hunk) whose content is identical, so renderIdentity-keyed caches, syntax and
// layout survive a refresh that only touched other files. Returns true only
// when the visible diff actually changed; an identical refresh leaves
// `current` untouched so callers can skip patchGeneration/render invalidation.
inline bool reconcile(std::vector<ecs::FileDiff>& current, std::vector<ecs::FileDiff> next) {
    if (current.size() == next.size()) {
        bool identical = true;
        for (size_t i = 0; i < current.size(); ++i)
            if (!file_equal(current[i], next[i])) { identical = false; break; }
        if (identical) return false;
    }
    std::vector<bool> used(current.size(), false);
    std::vector<ecs::FileDiff> result;
    result.reserve(next.size());
    for (auto& file : next) {
        size_t match = current.size();
        for (size_t i = 0; i < current.size(); ++i)
            if (!used[i] && current[i].filePath == file.filePath && current[i].oldPath == file.oldPath) { match = i; break; }
        if (match == current.size()) { result.push_back(std::move(file)); continue; }
        used[match] = true;
        if (file_equal(current[match], file)) { result.push_back(std::move(current[match])); continue; }
        for (auto& hunk : file.hunks)
            for (auto& old : current[match].hunks)
                if (hunk_equal(old, hunk)) { hunk.renderIdentity = old.renderIdentity; break; }
        result.push_back(std::move(file));
    }
    current = std::move(result);
    return true;
}

inline bool commit_log_equal(const std::vector<ecs::CommitEntry>& a, const std::vector<ecs::CommitEntry>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].hash != b[i].hash || a[i].subject != b[i].subject || a[i].author != b[i].author ||
            a[i].authorDate != b[i].authorDate || a[i].decorations != b[i].decorations ||
            a[i].parentHashes != b[i].parentHashes)
            return false;
    return true;
}

inline bool branches_equal(const std::vector<ecs::BranchInfo>& a, const std::vector<ecs::BranchInfo>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].name != b[i].name || a[i].shortHash != b[i].shortHash || a[i].isLocal != b[i].isLocal ||
            a[i].isCurrent != b[i].isCurrent || a[i].upstream != b[i].upstream || a[i].tracking != b[i].tracking)
            return false;
    return true;
}

inline bool status_files_equal(const std::vector<ecs::FileStatus>& a, const std::vector<ecs::FileStatus>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].path != b[i].path || a[i].indexStatus != b[i].indexStatus ||
            a[i].workTreeStatus != b[i].workTreeStatus || a[i].origPath != b[i].origPath ||
            a[i].additions != b[i].additions || a[i].deletions != b[i].deletions || a[i].isSubmodule != b[i].isSubmodule)
            return false;
    return true;
}

} // namespace diff_reconcile
