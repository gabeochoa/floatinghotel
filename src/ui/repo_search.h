#pragma once

#include "../ecs/ui_imports.h"
#include "../git/git_parser.h"
#include "../git/repository_search.h"
#include "../util/diff_revisions.h"
#include "tooltip.h"
#include "focus.h"

namespace ecs {

inline void render_repo_search(UIContext<InputAction>& ctx, Entity& parent,
                                RepoComponent& repo, LayoutComponent& layout) {
    ui::bind_focus(parent, repo, reading::focus::Region::Search);
    using namespace std::chrono_literals;
    if (repo.repoSearchPreviewFuture.valid() && repo.repoSearchPreviewFuture.wait_for(0s) == std::future_status::ready) {
        auto result = repo.repoSearchPreviewFuture.get();
        if (navigation::accepts(repo, repo.repoSearchPreviewFutureStamp, repo.repoSearchPreviewFutureStamp.key))
            repo.repoSearchPreview = std::move(result);
        repo.repoSearchPreviewFuture = {};
    }
    if (repo.repoSearchFuture.valid() && repo.repoSearchFuture.wait_for(0s) == std::future_status::ready) {
        auto result = repo.repoSearchFuture.get();
        repo.repoSearchFuture = {};
        if (navigation::accepts(repo, repo.repoSearchFutureStamp, repo.repoSearchQuery)) {
            repo.repoSearchError = std::move(result.error);
            repo.repoSearchRevision = std::move(result.revision);
            repo.repoSearchResults = std::move(result.matches);
            repo.repoSearchTruncated = result.truncated;
            repo.repoSearchCapturedBytes = result.capturedBytes;
        }
    }
    div(ctx, mk(parent, 587000), ComponentConfig{}
        .with_label("Search repository · " + (repo.repoSearchRevision.empty() ? "working tree" : repo.repoSearchRevision == "INDEX" ? "index" : repo.repoSearchRevision))
        .with_size(ComponentSize{percent(1.f), pixels(30)}).with_font_size(FontSize::Medium));
    bool scopeChanged = button(ctx, mk(parent, 587004), preset::Button(repo.repoSearchChangedOnly ? "Changed files only · deleted files use their previous revision" : "All repository files")
        .with_size(ComponentSize{percent(1.f), pixels(32)}).with_font_size(FontSize::Small)
        .with_custom_background(repo.repoSearchChangedOnly ? theme::BUTTON_PRIMARY : theme::BUTTON_SECONDARY)
        .with_debug_name("repo_search_changed_only"));
    if (scopeChanged) repo.repoSearchChangedOnly = !repo.repoSearchChangedOnly;
    auto matchingRow = div(ctx, mk(parent, 587005), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(32)}).with_flex_direction(FlexDirection::Row));
    auto& matching = repo.repoSearchMatching;
    bool matchingChanged = false;
    if (button(ctx, mk(matchingRow.ent(), 0), preset::Button(matching.regularExpression ? "Regular expression" : "Literal text")
            .with_custom_background(matching.regularExpression ? theme::BUTTON_PRIMARY : theme::BUTTON_SECONDARY)
            .with_size(ComponentSize{expand(), pixels(30)}).with_font_size(FontSize::Small).with_debug_name("repo_search_regex"))) {
        matching.regularExpression = !matching.regularExpression;
        matchingChanged = true;
    }
    if (button(ctx, mk(matchingRow.ent(), 1), preset::Button(matching.caseSensitive ? "Match case" : "Ignore case")
            .with_custom_background(!matching.caseSensitive ? theme::BUTTON_PRIMARY : theme::BUTTON_SECONDARY)
            .with_size(ComponentSize{expand(), pixels(30)}).with_font_size(FontSize::Small).with_debug_name("repo_search_case"))) {
        matching.caseSensitive = !matching.caseSensitive;
        matchingChanged = true;
    }
    if (button(ctx, mk(matchingRow.ent(), 2), preset::Button(matching.wholeWord ? "Whole words" : "Any substring")
            .with_custom_background(matching.wholeWord ? theme::BUTTON_PRIMARY : theme::BUTTON_SECONDARY)
            .with_size(ComponentSize{expand(), pixels(30)}).with_font_size(FontSize::Small).with_debug_name("repo_search_word"))) {
        matching.wholeWord = !matching.wholeWord;
        matchingChanged = true;
    }
    auto pathRow = div(ctx, mk(parent, 587006), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(34)}).with_flex_direction(FlexDirection::Row));
    div(ctx, mk(pathRow.ent(), 0), ComponentConfig{}.with_label("Include glob")
        .with_size(ComponentSize{pixels(90), pixels(32)}).with_font_size(FontSize::Small));
    auto include = afterhours::text_input::text_input(ctx, mk(pathRow.ent(), 1), repo.repoSearchIncludeGlob,
        ComponentConfig{}.with_size(ComponentSize{expand(), pixels(32)}).with_debug_name("repo_search_include"));
    div(ctx, mk(pathRow.ent(), 2), ComponentConfig{}.with_label("Exclude glob")
        .with_size(ComponentSize{pixels(90), pixels(32)}).with_font_size(FontSize::Small));
    auto exclude = afterhours::text_input::text_input(ctx, mk(pathRow.ent(), 3), repo.repoSearchExcludeGlob,
        ComponentConfig{}.with_size(ComponentSize{expand(), pixels(32)}).with_debug_name("repo_search_exclude"));
    ui::set_tooltip(include.ent(), "One repository-relative glob, for example src/** or **/*.cpp. Blank includes all paths.");
    ui::set_tooltip(exclude.ent(), "One repository-relative glob, for example **/fixtures/**. Blank excludes no paths.");
    auto row = div(ctx, mk(parent, 587001), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(34)}).with_flex_direction(FlexDirection::Row));
    auto input = afterhours::text_input::text_input(ctx, mk(row.ent(), 0), repo.repoSearchQuery,
        ComponentConfig{}.with_size(ComponentSize{pixels(std::max(80.f, layout.mainContent.width - 90.f)), pixels(32)})
            .with_debug_name("repo_search_input"));
    if (repo.repoSearchFocus) { ui::focus_control(ctx, input.ent()); repo.repoSearchFocus = false; }
    auto search = button(ctx, mk(row.ent(), 1), preset::Button("Search")
        .with_size(ComponentSize{pixels(86), pixels(32)}).with_debug_name("repo_search_submit"));
    if ((search || scopeChanged || matchingChanged || (!ui::shortcuts_blocked(layout) && ui::shortcut_owner(ctx, repo).input(reading::focus::Region::Search) && afterhours::input::is_key_pressed(257))) && !repo.repoSearchQuery.empty()) {
        repo.repoSearchResults.clear();
        repo.repoSearchPreviewOpen = false;
        repo.repoSearchPreviewFuture = {};
        repo.repoSearchError.clear();
        repo.repoSearchTruncated = false;
        repo.repoSearchCapturedBytes = 0;
        repo.repoSearchPath = repo.repoPath;
        repo.repoSearchRevision = source_tab_active(repo) ? repo.fullFileRevision() :
            repo.comparisonOpen() ? diff_revisions(repo.comparisonScope()).second :
            !repo.selectedCommitHash().empty() ? repo.selectedCommitHash() : repo.selectedFileStaged() ? "INDEX" : "";
        SearchQuery query{repo.repoPath, repo.repoSearchRevision, repo.repoSearchQuery};
        query.matching = matching;
        query.includeGlob = repo.repoSearchIncludeGlob;
        query.excludeGlob = repo.repoSearchExcludeGlob;
        query.changedOnly = repo.repoSearchChangedOnly;
        if (query.changedOnly) {
            const auto* changes = repo.selectedFileStaged() ? &repo.stagedDiff : &repo.currentDiff;
            query.beforeRevision = query.revision.empty() ? "INDEX" : query.revision == "INDEX" ? "HEAD" : query.revision + "^";
            if (repo.comparisonOpen()) {
                changes = &repo.comparisonDiff;
                query.beforeRevision = diff_revisions(repo.comparisonScope()).first;
            } else if (!repo.selectedCommitHash().empty()) {
                auto* detail = find_singleton<CommitDetailCache, ActiveTab>();
                changes = detail && detail->cachedCommitHash == repo.selectedCommitHash() ? &detail->commitDetailDiff : nullptr;
            }
            if (changes) {
                for (const auto& file : *changes)
                    (file.isDeleted ? query.removedPaths : query.paths).push_back(file.isDeleted && !file.oldPath.empty() ? file.oldPath : file.filePath);
                if (query.revision.empty() && !repo.comparisonOpen() && repo.selectedCommitHash().empty())
                    query.paths.insert(query.paths.end(), repo.untrackedFiles.begin(), repo.untrackedFiles.end());
            } else repo.repoSearchError = "Open the commit changes before searching changed files";
        }
        repo.repoSearchFuture = {};
        repo.repoSearchFutureStamp = navigation::stamp(repo, repo.repoSearchQuery);
        if (repo.repoSearchError.empty()) repo.repoSearchFuture = git::search_repository_async(std::move(query));
    }
    std::string status = repo.repoSearchFuture.valid() ? "Searching..." : repo.repoSearchResults.empty() ? "No matches" :
        std::to_string(repo.repoSearchResults.size()) + " matches";
    if (!repo.repoSearchFuture.valid() && repo.repoSearchTruncated)
        status = std::to_string(repo.repoSearchResults.size()) + " matches · Results limited to 5000 matches or 4 MiB; refine your search";
    if (!repo.repoSearchError.empty()) status = repo.repoSearchError;
    div(ctx, mk(parent, 587002), ComponentConfig{}.with_label(status)
        .with_size(ComponentSize{percent(1.f), pixels(30)}).with_font_size(FontSize::Small)
        .with_debug_name("repo_search_status"));
    const bool showPreview = repo.repoSearchPreviewOpen;
    afterhours::ui::imm::virtual_list(ctx, mk(parent, 587003), repo.repoSearchResults.size(), 32.f,
        [&](size_t i, Entity& item) {
            const auto& match = repo.repoSearchResults[i];
            auto resultRow = div(ctx, mk(item, 10), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(32)}).with_flex_direction(FlexDirection::Row));
            ui::bind_focus(resultRow.ent(), repo, reading::focus::Region::Search, match.file + ":" + std::to_string(match.line));
            auto previewText = match.text.substr(0, 512) + (match.text.size() > 512 ? "…" : "");
            if (button(ctx, mk(resultRow.ent(), 0), preset::Button(match.file + ":" + std::to_string(match.line) + "  " + previewText)
                    .with_size(ComponentSize{expand(), pixels(32)}).with_alignment(TextAlignment::Left)
                    .with_font_size(FontSize::Small).with_custom_background(theme::PANEL_BG)
                    .with_debug_name("repo_search_result"))) {
                navigation::click(repo, reading::source(match.file, match.revision, match.line), afterhours::input::is_key_pressed(257), reading::ClickRegion::Search);
            }
            if (button(ctx, mk(resultRow.ent(), 1), preset::Button("Preview")
                    .with_size(ComponentSize{pixels(80), pixels(30)}).with_font_size(FontSize::Small)
                    .with_debug_name("repo_search_preview"))) {
                repo.repoSearchPreviewOpen = true;
                repo.repoSearchPreview = SearchPreview{match};
                repo.repoSearchPreviewFutureStamp = navigation::stamp(repo, match.file + "\n" + match.revision + ":" + std::to_string(match.line));
                repo.repoSearchPreviewFuture = git::search_preview_async(repo.repoPath, match);
            }
        }, ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(std::max(40.f, layout.mainContent.height - 192.f - (showPreview ? 190.f : 0.f)))})
            .with_debug_name("repo_search_results"));
    if (showPreview) {
        const auto& preview = repo.repoSearchPreview;
        auto panel = div(ctx, mk(parent, 587007), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(190)}).with_custom_background(theme::PANEL_BG));
        ui::bind_focus(panel.ent(), repo, reading::focus::Region::SearchPreview);
        auto heading = div(ctx, mk(panel.ent(), 0), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(30)}).with_flex_direction(FlexDirection::Row));
        div(ctx, mk(heading.ent(), 0), ComponentConfig{}
            .with_label(preview.match.file + ":" + std::to_string(preview.match.line) + " · " +
                (preview.match.revision.empty() ? "working tree" : preview.match.revision == "INDEX" ? "index" : preview.match.revision))
            .with_size(ComponentSize{expand(), pixels(30)}).with_font_size(FontSize::Small));
        if (button(ctx, mk(heading.ent(), 1), preset::Button("Close preview")
                .with_size(ComponentSize{pixels(110), pixels(28)}).with_font_size(FontSize::Small)
                .with_debug_name("repo_search_preview_close"))) {
            repo.repoSearchPreviewOpen = false;
            repo.repoSearchPreviewFuture = {};
        }
        auto note = repo.repoSearchPreviewFuture.valid() ? "Loading nearby source..." :
            !preview.error.empty() ? preview.error : preview.changedSinceSearch ? "Source changed since this search; showing current source" : "Nearby source · two lines before and after";
        div(ctx, mk(panel.ent(), 1), ComponentConfig{}.with_label(note)
            .with_size(ComponentSize{percent(1.f), pixels(28)}).with_font_size(FontSize::Small));
        for (size_t i = 0; i < preview.lines.size(); ++i) {
            const auto& [line, source] = preview.lines[i];
            div(ctx, mk(panel.ent(), static_cast<int>(i) + 2), ComponentConfig{}
                .with_label(std::to_string(line) + "  " + source)
                .with_size(ComponentSize{percent(1.f), pixels(25)}).with_alignment(TextAlignment::Left)
                .with_font("mono", h720(14.f))
                .with_custom_background(line == preview.match.line ? theme::BUTTON_SECONDARY : theme::PANEL_BG)
                .with_debug_name("repo_search_preview_line"));
        }
    }
}

}
