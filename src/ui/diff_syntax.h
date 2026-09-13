#pragma once

#include "../git/diff_syntax.h"
#include "../util/navigation.h"
#include "../util/diff_revisions.h"

namespace ui::diff_syntax {

inline void update(ecs::RepoComponent& repo, std::vector<ecs::FileDiff>& files, const std::string& scope) {
    auto& runtime = repo.diffSyntax;
    auto pending = std::find_if(files.begin(), files.end(), [&](const auto& file) { return file.renderIdentity == runtime.identity; });
    if (runtime.future.valid() && (pending == files.end() || !navigation::accepts(repo, runtime.request, runtime.request.key))) runtime = {};
    if (runtime.future.valid() && runtime.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = runtime.future.get();
        if (result.error.empty() && result.seeds.size() == pending->hunks.size()) {
            for (size_t i = 0; i < result.seeds.size(); ++i)
                hunk_syntax::annotate(pending->hunks[i], pending->filePath, false,
                    result.seeds[i].first, result.seeds[i].second, pending->oldPath);
        }
        pending->syntaxResolved = true;
    }
    if (runtime.future.valid()) return;
    auto candidate = files.end();
    for (auto file = files.begin(); file != files.end(); ++file) {
        if (file->syntaxResolved || file->isFullContent || file->isBinary || file->hunks.empty()) continue;
        if (candidate == files.end()) candidate = file;
        if (file->filePath == repo.selectedFilePath()) { candidate = file; break; }
    }
    if (candidate == files.end()) return;
    const auto& file = *candidate;
    runtime.identity = file.renderIdentity;
    runtime.request = navigation::stamp(repo, "syntax:" + std::to_string(file.renderIdentity));
    std::vector<std::pair<int, int>> starts;
    for (const auto& hunk : file.hunks) starts.emplace_back(hunk.oldCount ? hunk.oldStart : 0, hunk.newCount ? hunk.newStart : 0);
    auto [oldRevision, newRevision] = diff_revisions(scope);
    git::FileRequest before{repo.repoPath, file.oldPath.empty() ? file.filePath : file.oldPath, oldRevision};
    git::FileRequest after{repo.repoPath, file.filePath, newRevision};
    runtime.future = async_work::launch([before, after, starts = std::move(starts)](std::stop_token stop) {
        return git::read_diff_syntax(before, after, starts, stop);
    }, async_work::Priority::Background, ecs::DiffSyntaxResult{{}, "Syntax reader is busy"});
}

}
