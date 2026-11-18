#pragma once

#include "../ecs/ui_imports.h"
#include "../git/git_parser.h"
#include "../git/repository_search.h"
#include "../util/diff_revisions.h"
#include "tooltip.h"
#include "token_cache.h"
#include "focus.h"
#include "virtual_list.h"
#include "chrome_icons.h"
#include "../util/code_wrap.h"

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
    else if (repo.comparisonOpen() && !repo.comparisonScope().empty()) {
        changes = &repo.comparisonDiff;
        scope.beforeRevision = diff_revisions(repo.comparisonScope()).first;
    } else if (!repo.selectedCommitHash().empty()) {
        const auto* detail = find_singleton<CommitDetailCache, ActiveTab>();
        changes = detail && detail->patch.key.repo == repo.repoPath && detail->patch.key.commit == repo.selectedCommitHash()
            ? &detail->commitDetailDiff : nullptr;
        if (changes && !detail->patch.key.parent.empty()) scope.beforeRevision = detail->patch.key.parent;
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
    if (!repo.repo_search_open()) repo.repoSearchRestoreScroll = true;
    repo.open_search();
    repo.repoSearchFocus = true;
    if (auto* layout = find_singleton<LayoutComponent>()) layout->readingPanelCollapsed = false;
}

inline void close_repo_search(RepoComponent& repo, LayoutComponent& layout) {
    for (const auto& point : layout.focus.returns)
        if (point.popup == reading::focus::Popup::Search && point.generation != repo.workspace().generation())
            navigation::focus_document(repo);
    repo.repoSearchPreviewOpen = false;
    if (repo.repoSearchFuture.valid()) repo.repoSearchDue = std::chrono::steady_clock::now();
    repo.close_search();
    repo.repoSearchFuture = {};
    repo.repoSearchPreviewFuture = {};
    repo.repoSearchRestoreScroll = true;
}

inline void clear_repo_search(RepoComponent& repo) {
    repo.repoSearchFuture = {};
    repo.repoSearchBatches.reset();
    repo.repoSearchPreviewFuture = {};
    repo.repoSearchPreviewOpen = false;
    repo.repoSearchResults.clear();
    repo.repoSearchGroups.clear();
    repo.repoSearchRows.clear();
    repo.repoSearchRebuildRows = false;
    repo.repoSearchSelected.reset();
    repo.repoSearchExpanded.reset();
    repo.repoSearchScroll = 0.f;
    repo.repoSearchRestoreScroll = true;
    repo.repoSearchError.clear();
    repo.repoSearchTruncated = false;
    repo.repoSearchCapturedBytes = 0;
    repo.repoSearchSource.clear();
}

inline void start_repo_search(RepoComponent& repo) {
    clear_repo_search(repo);
    repo.begin_search();
    repo.repoSearchPath = repo.repoPath;
    repo.repoSearchRequestGeneration = ++repo.repoSearchGeneration;
    if (repo.repoSearchChangedOnly && !repo.repoSearchChangesAvailable) {
        repo.fail_search("Load the review, then choose Use current document");
        return;
    }
    auto query = repo.repoSearchScope;
    query.text = repo.repoSearchQuery;
    query.matching = repo.repoSearchMatching;
    query.includeGlob = repo.repoSearchIncludeGlob;
    query.excludeGlob = repo.repoSearchExcludeGlob;
    query.changedOnly = repo.repoSearchChangedOnly;
    if (!query.changedOnly) { query.paths.clear(); query.removedPaths.clear(); }
    if (!query.text.empty()) {
        repo.repoSearchBatches = std::make_shared<SearchBatchQueue>();
        repo.repoSearchFuture = git::search_repository_async(std::move(query), repo.repoSearchBatches);
    } else {
        repo.finish_search();
    }
}

inline reading::SourceLocation repo_search_location(const RepoComponent& repo, const SearchMatch& match) {
    auto target = reading::source(match.file, match.revision, match.line);
    target.origin = repo.repoSearchOrigin;
    target.originAnchor = repo.repoSearchOriginAnchor;
    return target;
}

// One excerpt line (match ± context) with syntax colours from the shared
// token cache. Lines are tokenised individually, so block comments spanning
// the excerpt boundary can mis-colour - same approximation as the outline.
inline std::vector<afterhours::ui::TextSpan> repo_search_excerpt_spans(int lineNumber, const std::string& text, const std::string& path) {
    std::vector<afterhours::ui::TextSpan> spans{{std::to_string(lineNumber) + "  ", theme::TEXT_SECONDARY}};
    for (const auto& token : *code_highlight::token_cache().get(text, path)) {
        auto color = theme::TEXT_PRIMARY;
        switch (token.kind) {
            case code_highlight::Kind::Plain: break;
            case code_highlight::Kind::Keyword: color = theme::SYNTAX_KEYWORD; break;
            case code_highlight::Kind::String: color = theme::SYNTAX_STRING; break;
            case code_highlight::Kind::Number: color = theme::SYNTAX_NUMBER; break;
            case code_highlight::Kind::Comment: color = theme::SYNTAX_COMMENT; break;
        }
        if (!token.text.empty()) spans.push_back({token.text, color});
    }
    return spans;
}

inline std::vector<afterhours::ui::TextSpan> repo_search_match_label(const SearchMatch& match) {
    std::vector<afterhours::ui::TextSpan> spans{{std::to_string(match.line) + "  ", theme::TEXT_SECONDARY}};
    if (match.declaration) spans.push_back({"likely declaration  ", theme::TEXT_ACCENT});
    if (match.excerptStart) spans.push_back({"…", theme::TEXT_SECONDARY});
    const auto end = std::min(match.text.size(), match.excerptStart + match.highlighted.size());
    int previous = -1;
    for (auto at = match.excerptStart; at < end;) {
        const auto next = code_wrap::next_codepoint(match.text, at);
        if (next > end) break;
        const bool highlighted = match.highlighted.test(at - match.excerptStart);
        const auto color = highlighted ? theme::pick({190, 215, 255, 255}, {0, 80, 170, 255}) : theme::TEXT_PRIMARY;
        if (previous == static_cast<int>(highlighted)) spans.back().text += match.text.substr(at, next - at);
        else spans.push_back({match.text.substr(at, next - at), color});
        previous = static_cast<int>(highlighted);
        at = next;
    }
    if (end < match.text.size()) spans.push_back({"…", theme::TEXT_SECONDARY});
    return spans;
}

struct RepoSearchScrollOwner : afterhours::BaseComponent { int repository = -1; float zoom = 1.f; };

inline void render_repo_search(UIContext<InputAction>& ctx, Entity& parent,
                                RepoComponent& repo, LayoutComponent& layout, float width, float height) {
    ui::bind_focus_region(parent, repo, reading::focus::Region::Search);
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
    const auto owner = ui::shortcut_owner(ctx, repo);
    auto focused = afterhours::ui::UICollectionHolder::getEntityForID(ctx.focus_id);
    const auto focusTarget = focused.valid() ? ui::focus_target(**focused) : std::nullopt;
    const bool queryFocused = focusTarget && focusTarget->control == "repo_search_input";
    const bool enter = !ui::shortcuts_blocked(layout) && owner.input(reading::focus::Region::Search) && afterhours::input::is_key_pressed(afterhours::keys::ENTER);
    const auto now = std::chrono::steady_clock::now();
    if (repo.search_text_changed()) {
        clear_repo_search(repo);
        repo.debounce_search(now + std::chrono::milliseconds(150));
    }
    // Shift+Enter expands the selected excerpt inline; Alt+Enter opens
    // the source. Both consume the Enter plain search/open would use.
    const bool shiftDown = afterhours::input::is_key_down(afterhours::keys::LEFT_SHIFT) || afterhours::input::is_key_down(afterhours::keys::RIGHT_SHIFT);
    const bool altDown = afterhours::input::is_key_down(afterhours::keys::LEFT_ALT) || afterhours::input::is_key_down(afterhours::keys::RIGHT_ALT);
    bool excerptHandled = false;
    if (enter && repo.repoSearchSelected && (shiftDown || altDown) &&
        *repo.repoSearchSelected < repo.repoSearchResults.size()) {
        excerptHandled = true;
        const auto& match = repo.repoSearchResults[*repo.repoSearchSelected];
        if (altDown) navigation::click(repo, repo_search_location(repo, match), true, reading::ClickRegion::Search);
        else if (repo.repoSearchExpanded == repo.repoSearchSelected) repo.repoSearchExpanded.reset();
        else {
            repo.repoSearchExpanded = repo.repoSearchSelected;
            const auto cached = repo.repoSearchExcerptCache.find(match.file + "\n" + match.revision + "\n" + std::to_string(match.line));
            if (cached != repo.repoSearchExcerptCache.end() && cached->second.match.text == match.text) {
                repo.repoSearchPreview = cached->second;
                repo.repoSearchPreviewFuture = {};
            } else {
                repo.repoSearchPreview = SearchPreview{match};
                repo.repoSearchPreviewFuture = git::search_preview_async(repo.repoPath, match);
            }
        }
    }
    const bool keepSelected = enter && !excerptHandled && queryFocused && repo.repoSearchSelected.has_value();
    submit |= enter && !keepSelected && !excerptHandled;
    if (!repo.repoSearchQuery.empty() && (submit || (repo.repoSearchDue && now >= *repo.repoSearchDue))) {
        if (std::getenv("FH_TRACE_READING")) {
            const auto delay = repo.repoSearchDue ? std::chrono::duration<double, std::milli>(
                now - *repo.repoSearchDue + std::chrono::milliseconds(150)).count() : 0.;
            log_info("search_submit: reason={} delay_ms={:.2f} query={}", submit ? "explicit" : "pause", delay, repo.repoSearchQuery);
        }
        start_repo_search(repo);
    }
    using namespace std::chrono_literals;
    if (repo.repoSearchPreviewFuture.valid() && repo.repoSearchPreviewFuture.wait_for(0s) == std::future_status::ready) {
        auto result = repo.repoSearchPreviewFuture.get();
        if (repo.repoSearchPath == repo.repoPath && repo.repoSearchPreview.match.file == result.match.file &&
            repo.repoSearchPreview.match.revision == result.match.revision && repo.repoSearchPreview.match.line == result.match.line) {
            if (result.error.empty() && !result.changedSinceSearch) {
                if (repo.repoSearchExcerptCache.size() >= 64) repo.repoSearchExcerptCache.clear();
                repo.repoSearchExcerptCache[result.match.file + "\n" + result.match.revision + "\n" + std::to_string(result.match.line)] = result;
            }
            repo.repoSearchPreview = std::move(result);
        }
        repo.repoSearchPreviewFuture = {};
    }
    // Streamed batches land first; the final result replaces them with
    // the highlighted, declaration-tagged list when the search completes.
    if (repo.repoSearchBatches && repo.repoSearchPath == repo.repoPath &&
        repo.repoSearchRequestGeneration == repo.repoSearchGeneration &&
        repo.repoSearchSubmittedQuery == repo.repoSearchQuery) {
        std::vector<SearchMatch> drained;
        {
            std::lock_guard lock(repo.repoSearchBatches->mutex);
            drained.swap(repo.repoSearchBatches->pending);
        }
        if (!drained.empty()) {
            repo.repoSearchResults.insert(repo.repoSearchResults.end(),
                std::make_move_iterator(drained.begin()), std::make_move_iterator(drained.end()));
            repo.repoSearchGroups = search_results::group(repo.repoSearchResults);
            repo.repoSearchRebuildRows = true;
        }
    }
    if (repo.repoSearchFuture.valid() && repo.repoSearchFuture.wait_for(0s) == std::future_status::ready) {
        auto result = repo.repoSearchFuture.get();
        repo.repoSearchFuture = {};
        if (repo.repoSearchPath == repo.repoPath && repo.repoSearchRequestGeneration == repo.repoSearchGeneration &&
            repo.repoSearchSubmittedQuery == repo.repoSearchQuery) {
            repo.repoSearchError = std::move(result.error);
            repo.repoSearchScope.revision = std::move(result.revision);
            repo.repoSearchResults = std::move(result.matches);
            repo.repoSearchGroups = search_results::group(repo.repoSearchResults);
            // Identifier searches list files that seem to declare the name first.
            const auto& query = repo.repoSearchSubmittedQuery;
            if (repo.repoSearchMatching.wholeWord && !repo.repoSearchMatching.regularExpression &&
                !query.empty() && std::all_of(query.begin(), query.end(), symbol_outline::detail::ident)) {
                for (auto& match : repo.repoSearchResults) match.declaration = symbol_outline::declares(match.file, match.text, query);
                std::stable_partition(repo.repoSearchGroups.begin(), repo.repoSearchGroups.end(), [&](const auto& group) {
                    return std::any_of(group.matches.begin(), group.matches.end(), [&](size_t i) { return repo.repoSearchResults[i].declaration; });
                });
            }
            repo.repoSearchRebuildRows = true;
            repo.repoSearchTruncated = result.truncated;
            repo.repoSearchCapturedBytes = result.capturedBytes;
            repo.finish_search();
        } else if (repo.repoSearchPhase == Search::Running) {
            // Stale result (repo or query moved on): drop it, do not spin.
            repo.finish_search();
        }
    }
    if (repo.repoSearchRebuildRows) {
        repo.repoSearchRows = search_results::visible_rows(repo.repoSearchGroups);
        repo.repoSearchRebuildRows = false;
    }
    bool revealSelected = false;
    bool modified = false;
    for (int key = afterhours::keys::LEFT_SHIFT; key <= afterhours::keys::RIGHT_SUPER; ++key) modified |= afterhours::input::is_key_down(key);
    if (!ui::shortcuts_blocked(layout) && !modified && (queryFocused || (!owner.text && owner.region == reading::focus::Region::Search))) {
        const int direction = afterhours::input::is_key_pressed(afterhours::keys::DOWN) ? 1 : afterhours::input::is_key_pressed(afterhours::keys::UP) ? -1 : 0;
        if (direction) {
            const auto selected = search_results::adjacent_match(repo.repoSearchRows, repo.repoSearchSelected, direction);
            if (selected && selected != repo.repoSearchSelected) {
                repo.repoSearchSelected = selected;
                repo.repoSearchPreviewOpen = false;
                repo.repoSearchPreviewFuture = {};
                navigation::preview(repo, repo_search_location(repo, repo.repoSearchResults[*selected]));
                revealSelected = true;
            }
            ui::focus_control(ctx, input.ent());
        }
    }
    if (keepSelected) navigation::click(repo, repo_search_location(repo, repo.repoSearchResults[*repo.repoSearchSelected]), true, reading::ClickRegion::Search);
    std::string status;
    switch (repo.repoSearchPhase) {
        case Search::Debouncing: status = "Waiting for typing..."; break;
        case Search::Running: status = "Searching..."; break;
        case Search::Failed: status = repo.repoSearchError.empty() ? "Search failed" : repo.repoSearchError; break;
        case Search::External: status = repo.repoSearchSource; break;
        case Search::Closed:
        case Search::Done:
            status = repo.repoSearchQuery.empty() ? "Search repository contents" : repo.repoSearchResults.empty() ? "No matches" :
                std::to_string(repo.repoSearchResults.size()) + (repo.repoSearchMatching.wholeWord ? " matches · whole-word text, not references" : " matches");
            break;
    }
    if (repo.repoSearchPhase == Search::Done && repo.repoSearchTruncated) status += " · limit reached";
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
    if (revealSelected && listEntity.has<afterhours::ui::HasScrollView>()) {
        const auto selected = std::find_if(repo.repoSearchRows.begin(), repo.repoSearchRows.end(),
            [&](const auto& value) { return value.match == repo.repoSearchSelected; });
        auto& scroll = listEntity.get<afterhours::ui::HasScrollView>();
        const float rowHeight = 32.f * ui::zoom::get();
        const float top = static_cast<float>(selected - repo.repoSearchRows.begin()) * rowHeight;
        const float viewport = (available - previewHeight) * ui::zoom::get();
        float target = scroll.scroll_offset.y;
        if (top < target) target = top;
        else if (top + rowHeight > target + viewport) target = top + rowHeight - viewport;
        scroll.scroll_offset.y = scroll.scroll_target.y = scroll.last_eased_offset.y = std::max(0.f, target);
        scroll.anchor_child = -1;
        repo.repoSearchScroll = scroll.scroll_offset.y / ui::zoom::get();
    }
    ui::virtual_list(ctx, listParent, repo.repoSearchRows.size(),
        [&](size_t rowIndex) {
            const auto& row = repo.repoSearchRows[rowIndex];
            if (!row.match || repo.repoSearchExpanded != row.match) return 32.f;
            const auto& preview = repo.repoSearchPreview;
            const auto& match = repo.repoSearchResults[*row.match];
            const size_t lines = preview.match.file == match.file && preview.match.line == match.line ? preview.lines.size() : 0;
            return 32.f + static_cast<float>(lines) * 16.f + (lines ? 4.f : 0.f);
        },
        [&](size_t rowIndex, Entity& item) {
            const auto& row = repo.repoSearchRows[rowIndex];
            auto& group = repo.repoSearchGroups[row.group];
            if (!row.match) {
                const auto slash = group.file.find_last_of('/');
                const auto filename = slash == std::string::npos ? group.file : group.file.substr(slash + 1);
                const auto directory = slash == std::string::npos ? std::string{} : group.file.substr(0, slash);
                std::vector<afterhours::ui::TextSpan> spans{
                    {filename, theme::TEXT_PRIMARY},
                    {"  " + std::to_string(group.matches.size()), theme::TEXT_SECONDARY}};
                if (!directory.empty()) spans.push_back({"  " + directory, theme::TEXT_SECONDARY});
                if (group.revision != repo.repoSearchScope.revision)
                    spans.push_back({"  @" + group.revision.substr(0, 7), theme::TEXT_SECONDARY});
                auto header = button(ctx, mk(item, 11), preset::Button("")
                    .with_size(ComponentSize{percent(1.f), pixels(32)})
                    .with_flex_direction(FlexDirection::Row).with_align_items(AlignItems::Center)
                    .with_padding(Padding{.right = pixels(8), .left = pixels(4)})
                    .with_custom_background(theme::PANEL_BG).with_debug_name("repo_search_file"));
                ui::chrome_icon(ctx, mk(header.ent(), 0), group.collapsed ? ui::ChromeIcon::ChevronRight : ui::ChromeIcon::ChevronDown,
                    theme::TEXT_SECONDARY, "repo_search_file_chevron");
                auto fileLabel = div(ctx, mk(header.ent(), 1), ComponentConfig{}.with_styled_label(spans)
                    .with_size(ComponentSize{expand(), pixels(32)}).with_alignment(TextAlignment::Left)
                    .with_font_size(pixels(12)).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_debug_name("repo_search_file_label"));
                ui::bind_focus(header.ent(), repo, reading::focus::Region::Search, group.file + "@" + group.revision);
                ui::set_truncated_tooltip(header.ent(), group.file + " · " + (group.revision.empty() ? "Working tree" : group.revision), fileLabel.ent());
                if (header) {
                    group.collapsed = !group.collapsed;
                    repo.repoSearchRebuildRows = true;
                }
                return;
            }
            const auto i = *row.match;
            const auto& match = repo.repoSearchResults[i];
            const bool expanded = repo.repoSearchExpanded == row.match;
            if (expanded) {
                // Row heights are retained by the virtual list; re-measure
                // this row while its excerpt loads and lands.
                if (item.has<afterhours::ui::UIComponent>()) {
                    auto listEnt = afterhours::ui::UICollectionHolder::getEntityForID(item.get<afterhours::ui::UIComponent>().parent);
                    if (listEnt.valid()) afterhours::ui::imm::invalidate_virtual_rows(**listEnt, rowIndex, rowIndex + 1);
                }
            }
            const size_t excerptLines = expanded && repo.repoSearchPreview.match.file == match.file &&
                repo.repoSearchPreview.match.line == match.line ? repo.repoSearchPreview.lines.size() : 0;
            auto resultRow = div(ctx, mk(item, 10), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(expanded ? 32.f + static_cast<float>(excerptLines) * 16.f + 4.f : 32.f)})
                .with_flex_direction(FlexDirection::Column));
            ui::bind_focus(resultRow.ent(), repo, reading::focus::Region::Search, match.file + ":" + std::to_string(match.line));
            auto buttonRow = div(ctx, mk(resultRow.ent(), 2), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(32)}).with_flex_direction(FlexDirection::Row));
            const auto label = match.file + ":" + std::to_string(match.line) + "  " + match.text.substr(0, 512);
            auto result = button(ctx, mk(buttonRow.ent(), 0), preset::Button(label)
                .with_styled_label(repo_search_match_label(match))
                .with_size(ComponentSize{expand(), pixels(32)}).with_alignment(TextAlignment::Left)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_font_size(pixels(12))
                .with_custom_background(repo.repoSearchSelected == i ? theme::SELECTED_BG : theme::SIDEBAR_BG)
                .with_debug_name("repo_search_result"));
            ui::set_truncated_tooltip(result.ent(), label, result.ent());
            if (result) {
                repo.repoSearchSelected = i;
                navigation::click(repo, repo_search_location(repo, match), afterhours::input::is_key_pressed(afterhours::keys::ENTER), reading::ClickRegion::Search);
            }
            if (button(ctx, mk(buttonRow.ent(), 1), preset::Button("Preview")
                    .with_size(ComponentSize{pixels(60), pixels(30)}).with_font_size(pixels(12))
                    .with_debug_name("repo_search_preview"))) {
                repo.repoSearchSelected = i;
                repo.repoSearchOptionsOpen = false;
                repo.repoSearchPreviewOpen = true;
                const auto cached = repo.repoSearchExcerptCache.find(match.file + "\n" + match.revision + "\n" + std::to_string(match.line));
                if (cached != repo.repoSearchExcerptCache.end() && cached->second.match.text == match.text) {
                    repo.repoSearchPreview = cached->second;
                    repo.repoSearchPreviewFuture = {};
                } else {
                    repo.repoSearchPreview = SearchPreview{match};
                    repo.repoSearchPreviewFuture = git::search_preview_async(repo.repoPath, match);
                }
            }
            // Inline excerpt (Shift+Enter): match ± 2 lines, read-only.
            if (expanded && repo.repoSearchPreview.match.file == match.file && repo.repoSearchPreview.match.line == match.line) {
                const auto& preview = repo.repoSearchPreview;
                if (repo.repoSearchPreviewFuture.valid())
                    div(ctx, mk(resultRow.ent(), 3), ComponentConfig{}.with_label("Loading excerpt...")
                        .with_size(ComponentSize{percent(1.f), pixels(16)}).with_font_size(pixels(12))
                        .with_custom_text_color(theme::TEXT_SECONDARY).with_debug_name("repo_search_excerpt_loading"));
                for (size_t li = 0; li < preview.lines.size(); ++li) {
                    const auto& [lineNumber, text] = preview.lines[li];
                    div(ctx, mk(resultRow.ent(), static_cast<int>(10 + li)), ComponentConfig{}
                        .with_styled_label(repo_search_excerpt_spans(lineNumber, text, match.file))
                        .with_size(ComponentSize{percent(1.f), pixels(16)}).with_font_size(pixels(12))
                        .with_custom_background(lineNumber == match.line ? theme::SELECTED_BG : theme::SIDEBAR_BG)
                        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                        .with_debug_name("repo_search_excerpt_line"));
                }
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
