#pragma once

#include "../ecs/ui_imports.h"
#include "../git/git_parser.h"

namespace ecs {

inline void render_repo_search(UIContext<InputAction>& ctx, Entity& parent,
                                RepoComponent& repo, LayoutComponent& layout) {
    using namespace std::chrono_literals;
    if (repo.repoSearchFuture.valid() && repo.repoSearchFuture.wait_for(0s) == std::future_status::ready) {
        auto result = repo.repoSearchFuture.get();
        repo.repoSearchFuture = {};
        if (repo.repoSearchPath == repo.repoPath) {
            repo.repoSearchError = result.success() || result.exit_code() == 1 ? "" : result.stderr_str();
            repo.repoSearchResults = git::parse_grep_matches(result.stdout_str());
        }
    }
    div(ctx, mk(parent, 587000), ComponentConfig{}
        .with_label("Search repository · literal, case-sensitive")
        .with_size(ComponentSize{percent(1.f), pixels(30)}).with_font_size(FontSize::Medium));
    auto row = div(ctx, mk(parent, 587001), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(34)}).with_flex_direction(FlexDirection::Row));
    auto input = afterhours::text_input::text_input(ctx, mk(row.ent(), 0), repo.repoSearchQuery,
        ComponentConfig{}.with_size(ComponentSize{pixels(std::max(80.f, layout.mainContent.width - 90.f)), pixels(32)})
            .with_debug_name("repo_search_input"));
    if (repo.repoSearchFocus) { ctx.set_focus(input.ent().id); repo.repoSearchFocus = false; }
    auto search = button(ctx, mk(row.ent(), 1), preset::Button("Search")
        .with_size(ComponentSize{pixels(86), pixels(32)}).with_debug_name("repo_search_submit"));
    if ((search || afterhours::input::is_key_pressed(257)) && !repo.repoSearchQuery.empty()) {
        repo.repoSearchResults.clear();
        repo.repoSearchError.clear();
        repo.repoSearchPath = repo.repoPath;
        repo.repoSearchFuture = git::git_run_async(repo.repoPath,
            {"grep", "-n", "-I", "-z", "--untracked", "--exclude-standard", "--full-name", "-F", "-e", repo.repoSearchQuery, "--"}).share();
    }
    std::string status = repo.repoSearchFuture.valid() ? "Searching..." : repo.repoSearchResults.empty() ? "No matches" :
        std::to_string(repo.repoSearchResults.size()) + (repo.repoSearchResults.size() == 5000 ? " matches (first 5000 shown)" : " matches");
    if (!repo.repoSearchError.empty()) status = repo.repoSearchError;
    div(ctx, mk(parent, 587002), ComponentConfig{}.with_label(status)
        .with_size(ComponentSize{percent(1.f), pixels(30)}).with_font_size(FontSize::Small)
        .with_debug_name("repo_search_status"));
    afterhours::ui::imm::virtual_list(ctx, mk(parent, 587003), repo.repoSearchResults.size(), 32.f,
        [&](size_t i, Entity& item) {
            const auto& match = repo.repoSearchResults[i];
            if (button(ctx, mk(item, 0), preset::Button(match.file + ":" + std::to_string(match.line) + "  " + match.text)
                    .with_size(ComponentSize{percent(1.f), pixels(32)}).with_alignment(TextAlignment::Left)
                    .with_font_size(FontSize::Small).with_custom_background(theme::PANEL_BG)
                    .with_debug_name("repo_search_result"))) {
                repo.fullFilePath = repo.selectedFilePath = match.file;
                repo.selectedFileStaged = false;
                repo.selectedCommitHash.clear();
                repo.fullFileRevision.clear();
                repo.fullFileCacheKey.clear();
                repo.fullFileTargetLine = match.line;
                repo.fullFileNavigateFrames = 3;
                repo.repoSearchOpen = false;
                layout.diffFindOpen = false;
                ctx.set_focus(ctx.ROOT);
            }
        }, ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(std::max(40.f, layout.mainContent.height - 94.f))})
            .with_debug_name("repo_search_results"));
}

}
