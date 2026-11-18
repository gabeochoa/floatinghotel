#pragma once

#include "../git/diff_syntax.h"
#include "../util/navigation.h"
#include "../util/diff_revisions.h"

namespace ui::diff_syntax {

// git::read_syntax_seeds skips hunks that start on line 1 and files in a plain
// language, so a file where every side is one of those needs no read at all.
// New and untracked files are always like this.
inline bool needs_seeds(const ecs::FileDiff& file) {
    const auto plain = [](const std::string& path) { return code_lexer::language(path) == code_lexer::Language::Plain; };
    const bool oldPlain = plain(file.oldPath.empty() ? file.filePath : file.oldPath);
    const bool newPlain = plain(file.filePath);
    for (const auto& hunk : file.hunks) {
        if (!oldPlain && hunk.oldCount && hunk.oldStart > 1) return true;
        if (!newPlain && hunk.newCount && hunk.newStart > 1) return true;
    }
    return false;
}

// Files that need no seeds are annotated on the frame instead of queueing one
// background task each, one after another: 150 new files took ~11 s to settle.
// Capped per frame so a large batch does not hitch.
// Returns true when the budget ran out with files still waiting.
inline bool resolve_unseeded(std::vector<ecs::FileDiff>& files, size_t budget = 32) {
    for (auto& file : files) {
        if (file.syntaxResolved || file.isFullContent || file.isBinary || file.hunks.empty() || needs_seeds(file)) continue;
        if (!budget) return true;
        for (auto& hunk : file.hunks) hunk_syntax::annotate(hunk, file.filePath, false, {}, {}, file.oldPath);
        file.syntaxResolved = true;
        --budget;
    }
    return false;
}

inline void update(ecs::RepoComponent& repo, std::vector<ecs::FileDiff>& files, const std::string& scope) {
    // One runtime per scope: the combined staged+unstaged page updates both
    // in the same frame, and a shared slot made them discard each other.
    auto& runtime = repo.diffSyntax[scope];
    auto& slot = runtime.slot;
    auto pending = std::find_if(files.begin(), files.end(), [&](const auto& file) { return slot.key != 0 && file.renderIdentity == slot.key; });
    if (slot.task.valid() && (pending == files.end() || !navigation::accepts(repo, slot.stamp, slot.stamp.key))) slot.invalidate();
    if (auto landed = slot.poll(repo)) {
        auto result = std::move(*landed);
        if (pending != files.end()) {
            if (result.error.empty() && result.seeds.size() == pending->hunks.size()) {
                for (size_t i = 0; i < result.seeds.size(); ++i)
                    hunk_syntax::annotate(pending->hunks[i], pending->filePath, false,
                        result.seeds[i].first, result.seeds[i].second, pending->oldPath);
            }
            pending->syntaxResolved = true;
            if (!result.error.empty()) slot.fail(result.error);
        }
    } else if (slot.phase == async_slot::Phase::Failed && pending != files.end()) {
        // Honest absence: a failed seed read is not retried for this
        // renderIdentity, and the file is not left looking still-loading.
        pending->syntaxResolved = true;
    }
    resolve_unseeded(files);
    if (slot.busy()) return;
    auto candidate = files.end();
    for (auto file = files.begin(); file != files.end(); ++file) {
        if (file->syntaxResolved || file->isFullContent || file->isBinary || file->hunks.empty() || !needs_seeds(*file)) continue;
        // A Failed slot caches its key's failure: do not relaunch it.
        if (slot.phase == async_slot::Phase::Failed && slot.key == file->renderIdentity) continue;
        if (candidate == files.end()) candidate = file;
        if (file->filePath == repo.selectedFilePath()) { candidate = file; break; }
    }
    if (candidate == files.end()) return;
    const auto& file = *candidate;
    std::vector<std::pair<int, int>> starts;
    for (const auto& hunk : file.hunks) starts.emplace_back(hunk.oldCount ? hunk.oldStart : 0, hunk.newCount ? hunk.newStart : 0);
    auto [oldRevision, newRevision] = diff_revisions(scope);
    git::FileRequest before{repo.repoPath, file.oldPath.empty() ? file.filePath : file.oldPath, oldRevision};
    git::FileRequest after{repo.repoPath, file.filePath, newRevision};
    slot.request(file.renderIdentity, navigation::stamp(repo, "syntax:" + std::to_string(file.renderIdentity)),
        async_work::launch([before, after, starts = std::move(starts)](std::stop_token stop) {
            return git::read_diff_syntax(before, after, starts, stop);
        }, async_work::Priority::Background, ecs::DiffSyntaxResult{{}, "Syntax reader is busy"}));
}

}
