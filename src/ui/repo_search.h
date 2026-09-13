#pragma once

#include "../ecs/ui_imports.h"
#include "../git/git_parser.h"
#include "../git/repository_search.h"
#include "../util/diff_revisions.h"
#include "tooltip.h"
#include "focus.h"
#include "virtual_list.h"

namespace ecs {

inline void capture_repo_search_scope(RepoComponent& repo) {
    const auto& location = repo.workspace().location();
    const auto* document = repo.workspace().document(repo.workspace().active_id());
    const auto* source = std::get_if<reading::SourceLocation>(&location);
    repo.repoSearchOrigin = source ? source->origin : std::optional{std::get<reading::ReviewLocation>(location)};
    repo.repoSearchOriginAnchor = source ? source->originAnchor : document->anchor;
    auto& scope = repo.repoSearchScope;
    scope = {repo.repoPath, reading::revision_text(reading::source_revision_for(location)), {}};
    scope.beforeRevision = scope.revision.empty() ? "INDEX" : scope.revision == "INDEX" ? "HEAD" : scope.revision + "^";
    const auto* changes = repo.selectedFileStaged() ? &repo.stagedDiff : &repo.currentDiff;
    if (source && !scope.revision.empty() && scope.revision != "INDEX") {
        changes = repo.repoSearchOrigin && !std::holds_alternative<reading::WorkingChanges>(repo.repoSearchOrigin->destination)
            ? &repo.originFileSummaries : nullptr;
        if (changes) scope.beforeRevision = diff_revisions(reading::scope(*repo.repoSearchOrigin)).first;
    }
    else if (repo.comparisonOpen()) {
        changes = &repo.comparisonDiff;
        scope.beforeRevision = diff_revisions(repo.comparisonScope()).first;
    } else if (!repo.selectedCommitHash().empty()) {
        const auto* detail = find_singleton<CommitDetailCache, ActiveTab>();
        changes = detail && detail->cachedRepoPath == repo.repoPath && detail->cachedCommitHash == repo.selectedCommitHash()
            ? &detail->commitDetailDiff : nullptr;
        if (changes && !detail->cachedParentHash.empty()) scope.beforeRevision = detail->cachedParentHash;
    }
    repo.repoSearchChangesAvailable = changes != nullptr;
    if (changes) for (const auto& file : *changes) {
        const bool before = file.isDeleted || scope.revision == scope.beforeRevision;
        (file.isDeleted ? scope.removedPaths : scope.paths).push_back(before && !file.oldPath.empty() ? file.oldPath : file.filePath);
    }
    if (scope.revision.empty()) scope.paths.insert(scope.paths.end(), repo.untrackedFiles.begin(), repo.untrackedFiles.end());
}

inline void open_repo_search(RepoComponent& repo) {
    if (repo.repoPath.empty()) return;
    if (repo.repoSearchScope.repoPath != repo.repoPath) capture_repo_search_scope(repo);
    if (!repo.repoSearchOpen) repo.repoSearchRestoreScroll = true;
    repo.repoSearchOpen = repo.repoSearchFocus = true;
    if (auto* layout = find_singleton<LayoutComponent>()) layout->readingPanelCollapsed = false;
}

inline void close_repo_search(RepoComponent& repo, LayoutComponent& layout) {
    for (const auto& point : layout.focus.returns)
        if (point.popup == reading::focus::Popup::Search && point.generation != repo.workspace().generation())
            repo.readingFocusDocument = repo.workspace().active_id();
    repo.repoSearchOpen = repo.repoSearchPreviewOpen = false;
    repo.repoSearchFuture = {};
    repo.repoSearchPreviewFuture = {};
    repo.repoSearchRestoreScroll = true;
}

inline void start_repo_search(RepoComponent& repo) {
    repo.repoSearchFuture = {};
    repo.repoSearchPreviewFuture = {};
    repo.repoSearchPreviewOpen = false;
    repo.repoSearchResults.clear();
    repo.repoSearchSelected.reset();
    repo.repoSearchScroll = 0.f;
    repo.repoSearchRestoreScroll = true;
    repo.repoSearchError.clear();
    repo.repoSearchTruncated = false;
    repo.repoSearchCapturedBytes = 0;
    repo.repoSearchPath = repo.repoPath;
    repo.repoSearchSubmittedQuery = repo.repoSearchQuery;
    repo.repoSearchRequestGeneration = ++repo.repoSearchGeneration;
    if (repo.repoSearchChangedOnly && !repo.repoSearchChangesAvailable) {
        repo.repoSearchError = "Load the review, then choose Use current document";
        return;
    }
    auto query = repo.repoSearchScope;
    query.text = repo.repoSearchQuery;
    query.matching = repo.repoSearchMatching;
    query.includeGlob = repo.repoSearchIncludeGlob;
    query.excludeGlob = repo.repoSearchExcludeGlob;
    query.changedOnly = repo.repoSearchChangedOnly;
    if (!query.changedOnly) { query.paths.clear(); query.removedPaths.clear(); }
    if (!query.text.empty()) repo.repoSearchFuture = git::search_repository_async(std::move(query));
}

struct RepoSearchScrollOwner : afterhours::BaseComponent { int repository = -1; float zoom = 1.f; };

inline void render_repo_search(UIContext<InputAction>& ctx, Entity& parent,
                                RepoComponent& repo, LayoutComponent& layout, float width, float height) {
    ui::bind_focus_region(parent, repo, reading::focus::Region::Search);
    using namespace std::chrono_literals;
    if (repo.repoSearchPreviewFuture.valid() && repo.repoSearchPreviewFuture.wait_for(0s) == std::future_status::ready) {
        auto result = repo.repoSearchPreviewFuture.get();
        if (repo.repoSearchPath == repo.repoPath && repo.repoSearchPreview.match.file == result.match.file &&
            repo.repoSearchPreview.match.revision == result.match.revision && repo.repoSearchPreview.match.line == result.match.line)
            repo.repoSearchPreview = std::move(result);
        repo.repoSearchPreviewFuture = {};
    }
    if (repo.repoSearchFuture.valid() && repo.repoSearchFuture.wait_for(0s) == std::future_status::ready) {
        auto result = repo.repoSearchFuture.get();
        repo.repoSearchFuture = {};
        if (repo.repoSearchPath == repo.repoPath && repo.repoSearchRequestGeneration == repo.repoSearchGeneration &&
            repo.repoSearchSubmittedQuery == repo.repoSearchQuery) {
            repo.repoSearchError = std::move(result.error);
            repo.repoSearchScope.revision = std::move(result.revision);
            repo.repoSearchResults = std::move(result.matches);
            repo.repoSearchTruncated = result.truncated;
            repo.repoSearchCapturedBytes = result.capturedBytes;
        }
    }
    auto heading = div(ctx, mk(parent, 587000), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(28)}).with_flex_direction(FlexDirection::Row));
    div(ctx, mk(heading.ent(), 0), ComponentConfig{}.with_label("Search")
        .with_size(ComponentSize{expand(), pixels(28)}).with_font_size(pixels(14)));
    if (button(ctx, mk(heading.ent(), 1), preset::Button("Options")
            .with_size(ComponentSize{pixels(60), pixels(28)}).with_font_size(pixels(12)).with_debug_name("repo_search_options")))
        repo.repoSearchOptionsOpen = !repo.repoSearchOptionsOpen;
    if (button(ctx, mk(heading.ent(), 2), preset::Button("×")
            .with_size(ComponentSize{pixels(28), pixels(28)}).with_font_size(pixels(14)).with_debug_name("repo_search_close"))) {
        close_repo_search(repo, layout);
        return;
    }
    const auto& revision = repo.repoSearchScope.revision;
    const std::string scopeLabel = revision.empty() ? "Working tree" : revision == "INDEX" ? "Index" : "Commit " + revision.substr(0, 7);
    auto scopeLabelNode = div(ctx, mk(parent, 587010), ComponentConfig{}
        .with_label(scopeLabel + (repo.repoSearchChangedOnly ? " · changed files" : " · all files"))
        .with_size(ComponentSize{percent(1.f), pixels(24)}).with_font_size(pixels(12))
        .with_custom_text_color(theme::TEXT_SECONDARY).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
        .with_debug_name("repo_search_scope"));
    ui::set_tooltip(scopeLabelNode.ent(), revision.empty() ? "Working tree" : revision);
    auto row = div(ctx, mk(parent, 587001), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(32)}).with_flex_direction(FlexDirection::Row));
    auto input = afterhours::text_input::text_input(ctx, mk(row.ent(), 0), repo.repoSearchQuery,
        ComponentConfig{}.with_size(ComponentSize{expand(), pixels(32)}).with_debug_name("repo_search_input"));
    if (repo.repoSearchFocus) { ui::focus_control(ctx, input.ent()); repo.repoSearchFocus = false; }
    auto search = button(ctx, mk(row.ent(), 2), preset::Button("Search")
        .with_size(ComponentSize{pixels(60), pixels(32)}).with_font_size(pixels(12)).with_debug_name("repo_search_submit"));
    bool submit = static_cast<bool>(search);
    const float optionsHeight = repo.repoSearchOptionsOpen ? std::min(154.f, std::max(60.f, height - 148.f)) : 0.f;
    if (repo.repoSearchOptionsOpen) {
        auto options = div(ctx, mk(parent, 587030), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(optionsHeight)})
            .with_overflow(Overflow::Scroll, Axis::Y).with_debug_name("repo_search_options_scroll"));
        if (button(ctx, mk(options.ent(), 587004), preset::Button(repo.repoSearchChangedOnly ? "Changed files only" : "All repository files")
                .with_size(ComponentSize{percent(1.f), pixels(30)}).with_font_size(pixels(12))
                .with_debug_name("repo_search_changed_only"))) {
            repo.repoSearchChangedOnly = !repo.repoSearchChangedOnly;
            submit = true;
        }
        auto matchingRow = div(ctx, mk(options.ent(), 587005), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(30)}).with_flex_direction(FlexDirection::Row));
        auto& matching = repo.repoSearchMatching;
        for (int index = 0; index < 3; ++index) {
            bool* value = index == 0 ? &matching.regularExpression : index == 1 ? &matching.caseSensitive : &matching.wholeWord;
            const char* name = index == 0 ? "repo_search_regex" : index == 1 ? "repo_search_case" : "repo_search_word";
            if (button(ctx, mk(matchingRow.ent(), index), preset::Button(index == 0 ? "Regex" : index == 1 ? "Aa" : "Words")
                    .with_custom_background(*value ? theme::SELECTED_BG : theme::BUTTON_SECONDARY)
                    .with_size(ComponentSize{expand(), pixels(30)}).with_font_size(pixels(12)).with_debug_name(name))) {
                *value = !*value;
                submit = true;
            }
        }
        for (int index = 0; index < 2; ++index) {
            auto pathRow = div(ctx, mk(options.ent(), 587006 + index), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(32)}).with_flex_direction(FlexDirection::Row));
            div(ctx, mk(pathRow.ent(), 0), ComponentConfig{}.with_label(index == 0 ? "Include" : "Exclude")
                .with_size(ComponentSize{pixels(50), pixels(32)}).with_font_size(pixels(12)));
            auto field = afterhours::text_input::text_input(ctx, mk(pathRow.ent(), 1), index == 0 ? repo.repoSearchIncludeGlob : repo.repoSearchExcludeGlob,
                ComponentConfig{}.with_size(ComponentSize{expand(), pixels(32)})
                    .with_debug_name(index == 0 ? "repo_search_include" : "repo_search_exclude"));
            ui::set_tooltip(field.ent(), "Repository-relative glob, for example src/** or **/*.cpp");
        }
        if (button(ctx, mk(options.ent(), 587008), preset::Button("Use current document")
                .with_size(ComponentSize{percent(1.f), pixels(30)}).with_font_size(pixels(12)).with_debug_name("repo_search_current_scope"))) {
            capture_repo_search_scope(repo);
            submit = true;
        }
    }
    if (!ui::shortcuts_blocked(layout) && ui::shortcut_owner(ctx, repo).input(reading::focus::Region::Search) && afterhours::input::is_key_pressed(257)) submit = true;
    if (submit && !repo.repoSearchQuery.empty()) start_repo_search(repo);
    std::string status = repo.repoSearchFuture.valid() ? "Searching..." : repo.repoSearchResults.empty() ? "No matches" :
        std::to_string(repo.repoSearchResults.size()) + " matches";
    if (!repo.repoSearchFuture.valid() && repo.repoSearchTruncated) status += " · limit reached";
    if (!repo.repoSearchError.empty()) status = repo.repoSearchError;
    auto statusNode = div(ctx, mk(parent, 587002), ComponentConfig{}.with_label(status)
        .with_size(ComponentSize{percent(1.f), pixels(24)}).with_font_size(pixels(12))
        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("repo_search_status"));
    ui::set_tooltip(statusNode.ent(), repo.repoSearchTruncated ? "Results limited to 5000 matches or 4 MiB; refine your search" : status);
    const bool showPreview = repo.repoSearchPreviewOpen;
    const float available = std::max(40.f, height - 108.f - optionsHeight);
    const float previewHeight = showPreview ? std::min(190.f, available * .55f) : 0.f;
    const auto listParent = mk(parent, 587003);
    auto [listEntity, listOwner] = afterhours::ui::imm::deref(listParent);
    auto& scrollOwner = listEntity.addComponentIfMissing<RepoSearchScrollOwner>();
    const int repository = find_singleton_entity<RepoComponent, ActiveTab>()->id;
    if (listEntity.has<afterhours::ui::HasScrollView>()) {
        auto& scroll = listEntity.get<afterhours::ui::HasScrollView>();
        if (scrollOwner.repository != repository || scrollOwner.zoom != ui::zoom::get() || repo.repoSearchRestoreScroll)
            scroll.scroll_offset.y = scroll.scroll_target.y = scroll.last_eased_offset.y = repo.repoSearchScroll * ui::zoom::get();
        else repo.repoSearchScroll = scroll.scroll_offset.y / ui::zoom::get();
        scroll.anchor_child = -1;
        repo.repoSearchRestoreScroll = false;
    }
    scrollOwner.repository = repository;
    scrollOwner.zoom = ui::zoom::get();
    ui::virtual_list(ctx, listParent, repo.repoSearchResults.size(), 32.f,
        [&](size_t i, Entity& item) {
            const auto& match = repo.repoSearchResults[i];
            auto resultRow = div(ctx, mk(item, 10), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(32)}).with_flex_direction(FlexDirection::Row));
            ui::bind_focus(resultRow.ent(), repo, reading::focus::Region::Search, match.file + ":" + std::to_string(match.line));
            const auto label = match.file + ":" + std::to_string(match.line) + "  " + match.text.substr(0, 512);
            auto result = button(ctx, mk(resultRow.ent(), 0), preset::Button(label)
                .with_size(ComponentSize{expand(), pixels(32)}).with_alignment(TextAlignment::Left)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_font_size(pixels(12))
                .with_custom_background(repo.repoSearchSelected == i ? theme::SELECTED_BG : theme::SIDEBAR_BG)
                .with_debug_name("repo_search_result"));
            ui::set_tooltip(result.ent(), label);
            if (result) {
                repo.repoSearchSelected = i;
                auto target = reading::source(match.file, match.revision, match.line);
                target.origin = repo.repoSearchOrigin;
                target.originAnchor = repo.repoSearchOriginAnchor;
                navigation::click(repo, std::move(target), afterhours::input::is_key_pressed(257), reading::ClickRegion::Search);
            }
            if (button(ctx, mk(resultRow.ent(), 1), preset::Button("Preview")
                    .with_size(ComponentSize{pixels(60), pixels(30)}).with_font_size(pixels(12))
                    .with_debug_name("repo_search_preview"))) {
                repo.repoSearchSelected = i;
                repo.repoSearchOptionsOpen = false;
                repo.repoSearchPreviewOpen = true;
                repo.repoSearchPreview = SearchPreview{match};
                repo.repoSearchPreviewFuture = git::search_preview_async(repo.repoPath, match);
            }
        }, ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(available - previewHeight)})
            .with_debug_name("repo_search_results"));
    if (showPreview) {
        const auto& preview = repo.repoSearchPreview;
        auto panel = div(ctx, mk(parent, 587020), ComponentConfig{}
            .with_size(ComponentSize{pixels(width), pixels(previewHeight)}).with_overflow(Overflow::Scroll, Axis::Y)
            .with_custom_background(theme::PANEL_BG).with_debug_name("repo_search_preview_panel"));
        ui::bind_focus(panel.ent(), repo, reading::focus::Region::SearchPreview);
        auto headingRow = div(ctx, mk(panel.ent(), 0), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(30)}).with_flex_direction(FlexDirection::Row));
        div(ctx, mk(headingRow.ent(), 0), ComponentConfig{}.with_label(preview.match.file + ":" + std::to_string(preview.match.line))
            .with_size(ComponentSize{expand(), pixels(30)}).with_font_size(pixels(12)).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis));
        if (button(ctx, mk(headingRow.ent(), 1), preset::Button("×")
                .with_size(ComponentSize{pixels(28), pixels(28)}).with_font_size(pixels(14)).with_debug_name("repo_search_preview_close"))) {
            repo.repoSearchPreviewOpen = false;
            repo.repoSearchPreviewFuture = {};
        }
        const auto note = repo.repoSearchPreviewFuture.valid() ? "Loading nearby source..." : !preview.error.empty() ? preview.error :
            preview.changedSinceSearch ? "Source changed since search" : "Nearby source";
        div(ctx, mk(panel.ent(), 1), ComponentConfig{}.with_label(note)
            .with_size(ComponentSize{percent(1.f), pixels(28)}).with_font_size(pixels(12))
            .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis));
        for (size_t i = 0; i < preview.lines.size(); ++i) {
            const auto& [line, sourceText] = preview.lines[i];
            div(ctx, mk(panel.ent(), static_cast<int>(i) + 2), ComponentConfig{}
                .with_label(std::to_string(line) + "  " + sourceText)
                .with_size(ComponentSize{percent(1.f), pixels(25)}).with_alignment(TextAlignment::Left)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_font("mono", pixels(12))
                .with_custom_background(line == preview.match.line ? theme::BUTTON_SECONDARY : theme::PANEL_BG)
                .with_debug_name("repo_search_preview_line"));
        }
    }
}

}
