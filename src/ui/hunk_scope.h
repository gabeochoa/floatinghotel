#pragma once

#include "../git/source_find.h"
#include "../util/diff_revisions.h"

namespace ui::hunk_scope {

// The outline symbol nearest above a hunk's first change, from a text scan of
// the file at that side's revision (the old side only for deleted files).
// Approximate: an outline has no end lines. Empty until the scan finishes.
inline std::string enclosing(ecs::RepoComponent& repo, const ecs::FileDiff& file, const ecs::DiffHunk& hunk,
                             const std::string& scope) {
    if (scope == "snapshot" || file.isBinary || file.isSubmodule) return {};
    const bool before = file.isDeleted;
    const auto& path = before && !file.oldPath.empty() ? file.oldPath : file.filePath;
    if (!symbol_outline::supported(path)) return {};
    auto& runtime = repo.hunkScopes;
    if (runtime.future.valid() && runtime.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        if (runtime.outlines.size() >= 64) runtime.outlines.clear();  // bounded; rescans are cheap
        runtime.outlines[runtime.pending] = runtime.future.get().symbols;
    }
    const auto [oldRevision, newRevision] = diff_revisions(scope);
    const auto& revision = before ? oldRevision : newRevision;
    const auto key = scope + "\n" + path + "\n" + revision + "\n" + std::to_string(repo.dataGeneration);
    const auto found = runtime.outlines.find(key);
    if (found == runtime.outlines.end()) {
        if (!runtime.future.valid()) {
            runtime.pending = key;
            runtime.future = git::outline_source_async({repo.repoPath, path, revision});
        }
        return {};
    }
    int line = before ? hunk.oldStart : hunk.newStart;
    for (const auto& text : hunk.lines) {
        if (text.empty() || text[0] != ' ') break;
        ++line;
    }
    const symbol_outline::Symbol* nearest = nullptr;
    for (const auto& symbol : found->second) {
        if (symbol.line > line) break;
        nearest = &symbol;
    }
    return nearest ? nearest->name : std::string{};
}

}
