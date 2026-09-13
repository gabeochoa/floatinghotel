#pragma once

#include "../util/change_navigation.h"
#include "../util/navigation.h"
#include "../ecs/query_helpers.h"

namespace ui {

inline std::string navigate_change(ecs::RepoComponent& repo, ecs::ReviewComponent& review, int direction) {
    if (ecs::source_tab_active(repo)) return "Open a review to move between changes";
    const auto scope = reading::scope(repo.workspace().review());
    const std::vector<ecs::FileDiff>* files = nullptr;
    if (scope == "wt") files = &repo.currentDiff;
    else if (scope == "index") files = &repo.stagedDiff;
    else if (repo.comparisonOpen()) {
        if (repo.comparisonFuture.valid() || repo.comparisonLoadedScope != scope) return "Changes are still loading";
        files = &repo.comparisonDiff;
    } else {
        const auto* cache = ecs::find_singleton<ecs::CommitDetailCache, ecs::ActiveTab>();
        if (!cache || cache->patchFuture.valid() || cache->cachedCommitHash != repo.selectedCommitHash() ||
            cache->cachedParentHash != ecs::selected_commit_parent(repo)) return "Changes are still loading";
        files = &cache->commitDetailDiff;
    }
    auto order = ecs::visible_review_file_indices(*files, repo.fileFilter, &review, scope);
    auto changes = reading::change_locations(*files, order, reading::anchor_revision(repo.workspace().location()));
    std::erase_if(changes, [&](const auto& change) {
        return change.hunk && !review.showApproved && review.approvedHunks.contains(scope + "\n" +
            ecs::ReviewComponent::hunk_key((*files)[change.file].filePath, (*files)[change.file].hunks[*change.hunk]));
    });
    if (changes.empty()) return "No changes to navigate";
    const auto* document = repo.workspace().document(repo.workspace().active_id());
    const auto next = reading::adjacent_change(changes, *files, document->anchor, repo.selectedFilePath(), direction);
    if (!next) return direction > 0 ? "Last change" : "First change";
    const auto& destination = changes[*next];
    const auto& file = (*files)[destination.file];
    int cursor = 0;
    const bool selected = Settings::get().get_review_display_mode(repo.repoPath) == review_files::DisplayMode::SelectedFile;
    for (size_t i = 0; i < *next; ++i)
        if (changes[i].hunk && (!selected || changes[i].file == destination.file)) ++cursor;
    navigation::go_to_review_change(repo, review, file, destination.hunk ? std::optional{destination.anchor} : std::nullopt, cursor);
    return {};
}

}
