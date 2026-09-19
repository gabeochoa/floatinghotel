#pragma once

#include "../ecs/ui_imports.h"
#include "../util/fuzzy_match.h"
#include "focus.h"
#include "diff_renderer.h"
#include "virtual_list.h"
#include "../ecs/tab_bar_system.h"
#include <afterhours/src/plugins/modal.h>

namespace ecs {

inline std::string file_picker_revision_label(const reading::SourceRevision& revision) {
    if (std::holds_alternative<reading::WorkingTree>(revision)) return "Working tree";
    if (std::holds_alternative<reading::Index>(revision)) return "Staged";
    const auto& name = reading::revision_text(revision);
    return std::holds_alternative<reading::ObjectId>(revision) ? "Commit " + name.substr(0, 7) : name;
}

inline void update_file_picker_paths(RepoComponent& repo, LayoutComponent& layout) {
    auto& scope = layout.filePickerScope;
    const reading::SourceRevision revision = scope.workingTree ? reading::SourceRevision{reading::WorkingTree{}} : scope.documentRevision;
    std::string key = "paths:" + reading::revision_text(revision) + ":" + std::to_string(repo.dataGeneration);
    if (std::holds_alternative<reading::WorkingTree>(revision) || std::holds_alternative<reading::Index>(revision))
        key += ":" + std::to_string(repo.repoVersion) + ":" + std::to_string(repo.allFilePathsGeneration);
    if (scope.request.key != key || !navigation::accepts(repo, scope.request, key)) {
        scope.future = {};
        scope.listing = git::PathList{revision};
        scope.request = navigation::stamp(repo, key);
        layout.filePickerCacheKey.clear();
        if (!std::holds_alternative<reading::WorkingTree>(revision))
            scope.future = git::load_paths_async(repo.repoPath, revision);
        scope.changesFuture = {};
        scope.changes = {};
        if (std::holds_alternative<reading::ObjectId>(revision)) {
            std::string before;
            if (const auto* review = std::get_if<reading::ReviewLocation>(&repo.workspace().location())) {
                if (const auto* comparison = std::get_if<reading::ComparisonReview>(&review->destination)) before = reading::revision_text(comparison->before);
                if (const auto* commit = std::get_if<reading::CommitReview>(&review->destination); commit && commit->parent) before = reading::revision_text(*commit->parent);
            }
            scope.changesFuture = async_work::launch([path = repo.repoPath, oid = reading::revision_text(revision), before](std::stop_token stop) {
                return git::catalog::changes(path, oid, before, stop);
            }, async_work::Priority::Background, git::catalog::Changes{{}, "", "Change status unavailable"});
        }
    }
    if (scope.changesFuture.valid() && scope.changesFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = scope.changesFuture.get();
        if (navigation::accepts(repo, scope.request, key)) { scope.changes = std::move(result); ++scope.catalogVersion; }
    }
    if (scope.future.valid() && scope.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = scope.future.get();
        if (layout.filePickerOpen && navigation::accepts(repo, scope.request, key)) {
            scope.listing = std::move(result);
            layout.filePickerCacheKey.clear();
        }
    }
}

inline std::vector<afterhours::ui::TextSpan> file_picker_label(const std::string& path, const std::string& query, bool selected) {
    const auto ranges = fuzzy::matched_ranges(query, path);
    const auto filename = fuzzy::filename_start(path);
    std::vector<afterhours::ui::TextSpan> spans;
    const auto directoryColor = selected ? afterhours::Color{196, 202, 212, 255} : theme::TEXT_SECONDARY;
    auto append = [&](size_t begin, size_t finish) {
        size_t match = 0;
        int previousStyle = -1;
        for (size_t at = begin; at < finish;) {
            const auto end = code_wrap::next_codepoint(path, at);
            while (match < ranges.size() && ranges[match].second <= at) ++match;
            const bool highlighted = match < ranges.size() && ranges[match].first <= at;
            const auto color = highlighted ? afterhours::Color{190, 215, 255, 255} :
                at < filename ? directoryColor : theme::TEXT_PRIMARY;
            const int style = highlighted ? 2 : at < filename ? 0 : 1;
            if (previousStyle == style) spans.back().text += path.substr(at, end - at);
            else spans.push_back({path.substr(at, end - at), color});
            previousStyle = style;
            at = end;
        }
    };
    append(filename, path.size());
    if (filename > 0) {
        spans.push_back({"  ", directoryColor});
        append(0, filename - 1);
    }
    return spans;
}

inline const std::vector<FileDiff>* line_picker_diffs(const RepoComponent& repo) {
    const auto& target = repo.workspace().review().destination;
    if (const auto* working = std::get_if<reading::WorkingChanges>(&target))
        return working->staged ? &repo.stagedDiff : &repo.currentDiff;
    if (std::holds_alternative<reading::ComparisonReview>(target)) return &repo.comparisonDiff;
    const auto* cache = find_singleton<CommitDetailCache, ActiveTab>();
    return cache && cache->cachedRepoPath == repo.repoPath && cache->cachedCommitHash == repo.selectedCommitHash()
        ? &cache->commitDetailDiff : nullptr;
}

inline void open_line_picker(RepoComponent& repo, LayoutComponent& layout) {
    layout.filePickerPosition = {};
    auto& position = layout.filePickerPosition;
    position.lineMode = true;
    const auto* document = repo.workspace().document(repo.workspace().active_id());
    auto point = document->anchor.value_or(reading::ReadingAnchor{});
    point.revision = reading::anchor_revision(document->location);
    point.viewportFraction = .15f;
    if (const auto* source = std::get_if<reading::SourceLocation>(&document->location)) {
        point.path = source->destination.path;
        point.side = reading::DiffSide::After;
    } else {
        const auto& review = std::get<reading::ReviewLocation>(document->location);
        if (!review.file.empty() && point.path != review.file) {
            point.path = review.file;
            point.side = reading::DiffSide::After;
        }
        if (point.path.empty() && document->files && !document->files->empty()) point.path = document->files->front().path;
        if (const auto* files = line_picker_diffs(repo)) for (const auto& file : *files)
            if (file.filePath == point.path && file.isDeleted) point.side = reading::DiffSide::Before;
    }
    if (!point.path.empty()) position.point = std::move(point);
    layout.filePickerScope = {};
    layout.filePickerOpen = layout.filePickerFocus = true;
}

inline bool poll_file_picker_position(RepoComponent& repo, LayoutComponent& layout, const std::string& key) {
    auto& position = layout.filePickerPosition;
    if (position.key != key) {
        position.future = {};
        position.error.clear();
        position.key = key;
    }
    if (!position.future.valid() || position.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
    auto result = position.future.get();
    if (!navigation::accepts(repo, position.request, position.request.key)) return false;
    position.error = std::move(result.error);
    if (!position.error.empty()) return false;
    const auto anchor = reading::ReadingAnchor{result.location.destination.path, reading::anchor_revision(result.location),
        reading::DiffSide::After, result.location.line, result.location.column, .15f};
    navigation::open(repo, result.location, {}, position.mode, anchor);
    return true;
}

inline void request_file_picker_position(RepoComponent& repo, LayoutComponent& layout,
                                         reading::SourceLocation target, reading::OpenMode mode) {
    auto& position = layout.filePickerPosition;
    position.error.clear();
    position.mode = mode;
    position.request = navigation::stamp(repo, position.key + "\n" + target.destination.path + ":" +
        std::to_string(target.line) + ":" + std::to_string(target.column));
    position.future = git::locate_source_position_async(repo.repoPath, std::move(target), repo.fullFileEncodingOverride);
}

inline void render_line_picker(UIContext<InputAction>& ctx, Entity& parent, RepoComponent& repo, LayoutComponent& layout) {
    auto& position = layout.filePickerPosition;
    ui::bind_focus(parent, repo, reading::focus::Region::Picker);
    div(ctx, mk(parent, 587000), ComponentConfig{}.with_label("Go to line")
        .with_size(ComponentSize{percent(1.f), pixels(28)}).with_font_size(pixels(14)));
    div(ctx, mk(parent, 587001), ComponentConfig{}
        .with_label(position.point ? position.point->path + (position.point->side == reading::DiffSide::Before ? " · before" : "") : "Select a file to go to a line")
        .with_size(ComponentSize{percent(1.f), pixels(24)}).with_font_size(pixels(12))
        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("line_picker_path"));
    auto input = afterhours::text_input::text_input(ctx, mk(parent, 587002), position.input,
        ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(32)})
            .with_debug_name("line_picker_input"));
    if (layout.filePickerFocus) { ui::focus_control(ctx, input.ent()); layout.filePickerFocus = false; }
    position.parsed = file_query::line(position.input);
    if (poll_file_picker_position(repo, layout, "line:" + position.input)) return;
    const bool valid = position.point && position.parsed.position && position.parsed.error.empty();
    const auto status = position.future.valid() ? "Checking line..." : !position.error.empty() ? position.error : position.parsed.error;
    div(ctx, mk(parent, 587003), ComponentConfig{}.with_label(status.empty() ? "Line[:column] · Enter to go · Esc to return" : status)
        .with_size(ComponentSize{percent(1.f), pixels(28)}).with_font_size(pixels(12))
        .with_custom_text_color(theme::TEXT_PRIMARY)
        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("line_picker_status"));
    const bool pressed = button(ctx, mk(parent, 587004), preset::Button("Go", valid && !position.future.valid())
        .with_size(ComponentSize{pixels(64), pixels(28)}).with_debug_name("line_picker_go"));
    const bool enter = ui::shortcut_owner(ctx, repo).input(reading::focus::Region::Picker) && afterhours::input::is_key_pressed(257);
    if (!valid || position.future.valid() || (!pressed && !enter)) return;
    auto point = *position.point;
    point.revision = reading::anchor_revision(repo.workspace().location());
    point.line = position.parsed.position->line;
    point.column = position.parsed.position->column;
    if (const auto* source = std::get_if<reading::SourceLocation>(&repo.workspace().location())) {
        auto target = *source;
        target.line = point.line;
        target.column = point.column;
        request_file_picker_position(repo, layout, std::move(target), reading::OpenMode::Keep);
    } else {
        const auto* files = line_picker_diffs(repo);
        auto* review = find_singleton<ReviewComponent, ActiveTab>();
        if (files && review) for (const auto& file : *files) {
            if (file.filePath != point.path) continue;
            if (const auto found = reading::position_in_diff(file, point)) {
                navigation::go_to_review_line(repo, *review, file, *found);
                return;
            }
        }
        position.error = "Line is not in this diff. Use Go to File to read its source.";
    }
}

inline void update_picker_catalog(RepoComponent& repo, LayoutComponent& layout) {
    auto& scope = layout.filePickerScope;
    const auto path = repo.repoPath;
    if (!scope.catalogStarted) {
        scope.catalogStarted = true;
        scope.localFuture = async_work::launch([path](std::stop_token stop) { return git::catalog::refs(path, false, stop); },
            async_work::Priority::Foreground, git::catalog::Page{{}, "Reader queue is full; reopen Quick Open"});
        scope.remoteFuture = async_work::launch([path](std::stop_token stop) { return git::catalog::refs(path, true, stop); },
            async_work::Priority::Background, git::catalog::Page{{}, "Reader queue is full; reopen Quick Open"});
        scope.catalogQuery = layout.filePickerQuery;
        scope.catalogDue = std::chrono::steady_clock::now();
    }
    if (scope.catalogQuery != layout.filePickerQuery) {
        scope.commitsFuture = {};
        scope.commits = {};
        scope.catalogQuery = layout.filePickerQuery;
        scope.catalogDue = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
        ++scope.catalogVersion;
    }
    if (scope.catalogDue && std::chrono::steady_clock::now() >= *scope.catalogDue) {
        scope.catalogDue.reset();
        scope.commitsFuture = async_work::launch([path, query = scope.catalogQuery](std::stop_token stop) {
            return git::catalog::commits(path, query, stop);
        }, async_work::Priority::Foreground, git::catalog::Page{{}, "Reader queue is full; change the query to retry"});
    }
    const bool extraCategory = scope.category == git::catalog::Kind::Reflog || scope.category == git::catalog::Kind::Stash || (scope.category == git::catalog::Kind::Worktree || scope.category == git::catalog::Kind::Submodule || scope.category == git::catalog::Kind::Diagnostic);
    if (!extraCategory) {
        scope.extraFuture = {};
        scope.statusFuture = {};
        scope.extraKey.clear();
    }
    if (extraCategory) {
        const auto key = std::to_string(static_cast<int>(scope.category)) + ":" + (scope.category == git::catalog::Kind::Reflog ? layout.filePickerQuery : "");
        if (key != scope.extraKey || scope.moreRequested) {
            const bool append = key == scope.extraKey;
            scope.extraOffset = append ? scope.extra.entries.size() : 0;
            if (!append) scope.extra = {};
            scope.extraKey = key;
            scope.moreRequested = false;
            scope.extraFuture = async_work::launch([path, kind = scope.category, query = layout.filePickerQuery, offset = scope.extraOffset](std::stop_token stop) {
                if (kind == git::catalog::Kind::Reflog) return git::catalog::reflog(path, offset, query, stop);
                if (kind == git::catalog::Kind::Stash) return git::catalog::stashes(path, stop, offset);
                if (kind == git::catalog::Kind::Diagnostic) return git::catalog::diagnostics(path, stop);
                if (kind == git::catalog::Kind::Submodule) return git::catalog::submodules(path, stop);
                return git::catalog::worktrees(path, stop);
            }, async_work::Priority::Foreground, git::catalog::Page{{}, "Reader queue is full; retry"});
        }
        if (scope.extraFuture.valid() && scope.extraFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto result = scope.extraFuture.get();
            scope.extra.error = std::move(result.error);
            scope.extra.more = result.more;
            scope.extra.entries.insert(scope.extra.entries.end(), std::make_move_iterator(result.entries.begin()), std::make_move_iterator(result.entries.end()));
            ++scope.catalogVersion;
        }
        if ((scope.category == git::catalog::Kind::Worktree || scope.category == git::catalog::Kind::Submodule)) {
            if (scope.statusFuture.valid() && scope.statusFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                scope.worktreeStatus[scope.statusPath] = scope.statusFuture.get();
                ++scope.catalogVersion;
            }
            if (!scope.statusFuture.valid()) for (const auto& entry : scope.extra.entries) {
                if (scope.worktreeStatus.contains(entry.identity)) continue;
                scope.statusPath = entry.identity;
                scope.statusFuture = async_work::launch([path = entry.identity, kind = entry.kind](std::stop_token stop) { return kind == git::catalog::Kind::Submodule ? git::catalog::submodule_status(path, stop) : git::catalog::worktree_status(path, stop); },
                    async_work::Priority::Background, std::string("Status unavailable: reader queue full"));
                break;
            }
        }
    }
    auto poll = [&](auto& future, auto& page) {
        if (!future.valid() || future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        auto result = future.get();
        if (!scope.owner || !navigation::accepts(repo, *scope.owner, scope.owner->key)) return;
        page = std::move(result);
        ++scope.catalogVersion;
    };
    poll(scope.localFuture, scope.localRefs);
    poll(scope.remoteFuture, scope.remoteRefs);
    poll(scope.commitsFuture, scope.commits);
}

inline void render_file_picker(UIContext<InputAction>& ctx, Entity& parent,
                                RepoComponent& repo, LayoutComponent& layout, float height) {
    bool revealSelection = layout.filePickerFocus;
    ui::bind_focus(parent, repo, reading::focus::Region::Picker);
    auto& scope = layout.filePickerScope;
    auto& position = layout.filePickerPosition;
    auto heading = div(ctx, mk(parent, 586000), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(30)}).with_flex_direction(FlexDirection::Row).with_gap(pixels(6)));
    div(ctx, mk(heading.ent(), 0), ComponentConfig{}.with_label("Quick Open")
        .with_size(ComponentSize{children(), pixels(30)}).with_font_size(pixels(14)));
    if (button(ctx, mk(heading.ent(), 1), preset::Button(file_picker_revision_label(scope.documentRevision))
            .with_size(ComponentSize{children(), pixels(28)})
            .with_custom_background(!scope.workingTree ? theme::SELECTED_BG : theme::PANEL_BG)
            .with_debug_name("file_picker_document_scope"))) {
        scope.workingTree = false;
        layout.filePickerFocus = true;
    }
    if (!std::holds_alternative<reading::WorkingTree>(scope.documentRevision) &&
        button(ctx, mk(heading.ent(), 2), preset::Button("Working tree")
            .with_size(ComponentSize{children(), pixels(28)})
            .with_custom_background(scope.workingTree ? theme::SELECTED_BG : theme::PANEL_BG)
            .with_debug_name("file_picker_working_scope"))) {
        scope.workingTree = true;
        layout.filePickerFocus = true;
    }
    if (!scope.listing.error.empty() && button(ctx, mk(heading.ent(), 3), preset::Button("Retry")
            .with_size(ComponentSize{children(), pixels(28)}).with_debug_name("file_picker_retry"))) {
        scope.request.key.clear();
        layout.filePickerFocus = true;
    }
    update_file_picker_paths(repo, layout);
    update_picker_catalog(repo, layout);
    const bool working = std::holds_alternative<reading::WorkingTree>(scope.listing.revision);
    const auto& paths = working ? repo.allFilePaths : scope.listing.paths;
    auto input = afterhours::text_input::text_input(ctx, mk(parent, 586001), layout.filePickerQuery,
        ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(32)}).with_debug_name("file_picker_input"));
    if (layout.filePickerFocus || ui::shortcut_owner(ctx, repo).region != reading::focus::Region::Picker) {
        ui::focus_control(ctx, input.ent());
        layout.filePickerFocus = false;
    }
    auto categories = div(ctx, mk(parent, 586004), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(28)}).with_flex_direction(FlexDirection::Row).with_gap(pixels(4)));
    const std::array kinds{git::catalog::Kind::All, git::catalog::Kind::File, git::catalog::Kind::Commit,
        git::catalog::Kind::Branch, git::catalog::Kind::Tag};
    for (size_t i = 0; i < kinds.size(); ++i) {
        if (button(ctx, mk(categories.ent(), static_cast<int>(i)), preset::Button(git::catalog::label(kinds[i]))
            .with_size(ComponentSize{children(), pixels(26)}).with_font_size(pixels(12))
            .with_custom_background(scope.category == kinds[i] ? theme::SELECTED_BG : theme::PANEL_BG)
            .with_debug_name("quick_open_category_" + git::catalog::label(kinds[i])))) {
            scope.category = kinds[i];
            layout.filePickerCacheKey.clear();
            layout.filePickerFocus = true;
        }
    }
    if (button(ctx, mk(categories.ent(), 10), preset::Button("More")
        .with_size(ComponentSize{children(), pixels(26)}).with_font_size(pixels(12)).with_debug_name("quick_open_more"))) {
        std::vector<ui::ContextMenuItem> items;
        for (const auto kind : {git::catalog::Kind::Reflog, git::catalog::Kind::Stash, git::catalog::Kind::Worktree, git::catalog::Kind::Submodule, git::catalog::Kind::Diagnostic})
            items.push_back(ui::ContextMenuItem::item(git::catalog::label(kind), [kind] {
                if (auto* active = find_singleton<LayoutComponent>()) {
                    active->filePickerScope.category = kind;
                    active->filePickerCacheKey.clear();
                    active->filePickerFocus = true;
                }
            }));
        ui::show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, std::move(items));
    }
    const auto selectionKey = repo.repoPath + "\n" + reading::revision_text(scope.listing.revision) + "\n" + layout.filePickerQuery;
    if (layout.filePickerSelectionKey != selectionKey) {
        layout.filePickerSelectedPath.clear();
        layout.filePickerSelectionKey = selectionKey;
    }
    std::string key = repo.repoPath + ":" + std::to_string(repo.dataGeneration) + ":" +
                      std::to_string(repo.repoVersion) + "\n" + scope.request.key + "\n" + layout.filePickerQuery + ":" + std::to_string(scope.catalogVersion) + ":" + std::to_string(static_cast<int>(scope.category));
    if (key != layout.filePickerCacheKey) {
        position.parsed = file_query::parse(layout.filePickerQuery, paths);
        scope.results.clear();
        if ((scope.category == git::catalog::Kind::All || scope.category == git::catalog::Kind::File) && position.parsed.error.empty()) {
            auto searchable = paths;
            auto changes = scope.changes;
            if (working || std::holds_alternative<reading::Index>(scope.listing.revision)) {
                changes.before = working ? "INDEX" : repo.headCommitHash;
                const auto& files = working ? repo.unstagedFiles : repo.stagedFiles;
                for (const auto& file : files) {
                    const char status = working ? file.workTreeStatus : file.indexStatus;
                    changes.paths[file.path] = status == 'A' ? "Added" : status == 'D' ? "Deleted" : status == 'R' ? "Renamed from " + file.origPath : "Modified";
                }
                if (working) for (const auto& path : repo.untrackedFiles) changes.paths[path] = "Untracked";
            }
            for (const auto& [path, status] : changes.paths)
                if (status == "Deleted" && std::find(searchable.begin(), searchable.end(), path) == searchable.end()) searchable.push_back(path);
            const auto ranked = fuzzy::rank(searchable, position.parsed.path, repo.workspace().recent_source_paths(scope.listing.revision));
            for (const auto& path : ranked) {
                const auto found = changes.paths.find(path);
                const auto status = found == changes.paths.end() ? "" : found->second;
                scope.results.push_back({git::catalog::Kind::File, path, path, status, status == "Deleted" ? changes.before : ""});
            }
        }
        if (!position.parsed.position) {
            std::vector<git::catalog::Entry> entries;
            entries.insert(entries.end(), scope.localRefs.entries.begin(), scope.localRefs.entries.end());
            entries.insert(entries.end(), scope.remoteRefs.entries.begin(), scope.remoteRefs.entries.end());
            entries.insert(entries.end(), scope.commits.entries.begin(), scope.commits.entries.end());
            auto matches = git::catalog::filter(entries, scope.category, layout.filePickerQuery);
            if (git::catalog::hash_query(layout.filePickerQuery))
                scope.results.insert(scope.results.begin(), matches.begin(), matches.end());
            else scope.results.insert(scope.results.end(), matches.begin(), matches.end());
        }
        if (scope.category == git::catalog::Kind::Reflog || scope.category == git::catalog::Kind::Stash || (scope.category == git::catalog::Kind::Worktree || scope.category == git::catalog::Kind::Submodule || scope.category == git::catalog::Kind::Diagnostic)) {
            scope.results = git::catalog::filter(scope.extra.entries, scope.category,
                scope.category == git::catalog::Kind::Reflog ? "" : layout.filePickerQuery);
            if (scope.category == git::catalog::Kind::Worktree || scope.category == git::catalog::Kind::Submodule)
                for (auto& entry : scope.results) entry.detail = (scope.worktreeStatus.contains(entry.identity) ? scope.worktreeStatus.at(entry.identity) : "Checking status...") + " · " + entry.detail;
        }
        layout.filePickerCacheKey = key;
        layout.filePickerIndex = 0;
        for (size_t i = 0; i < scope.results.size(); ++i)
            if (scope.results[i].key() == layout.filePickerSelectedPath) layout.filePickerIndex = static_cast<int>(i);
        revealSelection = true;
    }
    if (poll_file_picker_position(repo, layout, key)) return;
    auto open = [&](const git::catalog::Entry& entry, bool keep) {
        if (entry.kind == git::catalog::Kind::Diagnostic) { afterhours::clipboard::set_text(entry.title + ": " + entry.detail); return; }
        if (entry.kind == git::catalog::Kind::Submodule) {
            auto status = scope.worktreeStatus.find(entry.identity);
            if (status != scope.worktreeStatus.end() && status->second.starts_with("Checkout ")) TabBarSystem::open_repository(entry.identity, layout);
            return;
        }
        if (entry.kind == git::catalog::Kind::Worktree) {
            TabBarSystem::open_repository(entry.identity, layout);
            return;
        }
        if (entry.kind == git::catalog::Kind::Stash) {
            std::vector<ui::ContextMenuItem> items;
            for (const auto& portion : {std::string("Staged"), std::string("Working"), std::string("Untracked")}) {
                const auto before = portion == "Staged" ? entry.object + "^1" : portion == "Working" ? entry.object + "^2" : "empty";
                const auto after = portion == "Staged" ? entry.object + "^2" : portion == "Working" ? entry.object : entry.object + "^3";
                items.push_back(ui::ContextMenuItem::item("Review " + portion + " portion", [before, after, title = "Stash " + entry.object.substr(0, 7) + " · " + portion] {
                    if (auto* active = find_singleton<RepoComponent, ActiveTab>()) {
                        navigation::open(*active, reading::review("compare:" + before + ":" + after), true, reading::OpenMode::Keep);
                        navigation::remember_document_subject(*active, title);
                    }
                }, portion != "Untracked" || git::catalog::fields(entry.parents, ' ').size() > 2));
            }
            ui::show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, std::move(items));
            return;
        }
        if (entry.kind != git::catalog::Kind::File) {
            navigation::click(repo, reading::review(entry.object), keep, reading::ClickRegion::Picker);
            return;
        }
        auto target = reading::SourceLocation{{entry.identity, entry.object.empty() ? scope.listing.revision : reading::source_revision(entry.object)}};
        if (position.parsed.position) {
            target.line = position.parsed.position->line;
            target.column = position.parsed.position->column;
            request_file_picker_position(repo, layout, std::move(target), keep ? reading::OpenMode::Keep : reading::OpenMode::Preview);
        } else navigation::click(repo, std::move(target), keep, reading::ClickRegion::Picker);
    };
    auto& results = scope.results;
    const bool pickerKeys = !ui::shortcuts_blocked(layout) && ui::shortcut_owner(ctx, repo).input(reading::focus::Region::Picker);
    if (pickerKeys && !results.empty()) {
        if (afterhours::input::is_key_pressed(264)) layout.filePickerIndex = std::min(layout.filePickerIndex + 1, static_cast<int>(results.size()) - 1);
        if (afterhours::input::is_key_pressed(265)) layout.filePickerIndex = std::max(0, layout.filePickerIndex - 1);
        if (afterhours::input::is_key_pressed(257)) open(results[layout.filePickerIndex], true);
    }
    if (!results.empty()) layout.filePickerSelectedPath = results[layout.filePickerIndex].key();
    const bool includesFiles = scope.category == git::catalog::Kind::All || scope.category == git::catalog::Kind::File;
    const bool includesCommits = scope.category == git::catalog::Kind::All || scope.category == git::catalog::Kind::Commit;
    const bool includesRefs = scope.category == git::catalog::Kind::All || scope.category == git::catalog::Kind::Branch || scope.category == git::catalog::Kind::Tag;
    std::string error = includesFiles ? (working ? repo.filesError : scope.listing.error) : "";
    if (includesCommits && !scope.commits.error.empty()) error = scope.commits.error;
    if (includesRefs && !scope.localRefs.error.empty()) error = scope.localRefs.error;
    if (includesRefs && !scope.remoteRefs.error.empty() && scope.category != git::catalog::Kind::Tag) error = scope.remoteRefs.error;
    const bool searching = (includesCommits && (scope.commitsFuture.valid() || scope.catalogDue)) ||
        (includesRefs && (scope.localFuture.valid() || (scope.category != git::catalog::Kind::Tag && scope.remoteFuture.valid())));
    const std::string status = position.future.valid() ? "Checking line..." : !position.error.empty() ? position.error :
        !position.parsed.error.empty() && includesFiles ? position.parsed.error : !error.empty() ? error :
        includesFiles && scope.future.valid() ? "Loading files..." : searching ? std::to_string(results.size()) + " results · searching..." :
        std::to_string(results.size()) + (includesFiles && scope.listing.truncated ? " matches · file list limit reached" :
            includesRefs && (scope.localRefs.more || scope.remoteRefs.more) ? " matches · ref list limit reached" :
            includesCommits && scope.commits.more ? " matches · commit result limit reached" :
            layout.filePickerQuery.empty() && includesFiles ? " results · recent files first · Enter open · Esc close" :
            " matches · arrows to choose · Enter open · Esc close");
    div(ctx, mk(parent, 586002), ComponentConfig{}
        .with_label(status).with_debug_name("file_picker_status")
        .with_size(ComponentSize{percent(1.f), pixels(28)}).with_font_size(pixels(12))
        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis));
    const bool extraCategory = scope.category == git::catalog::Kind::Reflog || scope.category == git::catalog::Kind::Stash || (scope.category == git::catalog::Kind::Worktree || scope.category == git::catalog::Kind::Submodule || scope.category == git::catalog::Kind::Diagnostic);
    if (extraCategory && (scope.extraFuture.valid() || !scope.extra.error.empty() || scope.extra.more)) {
        const bool canLoadMore = scope.extra.entries.size() < 2000 && (scope.category == git::catalog::Kind::Stash || scope.category == git::catalog::Kind::Reflog);
        const auto text = scope.extraFuture.valid() ? "Loading " + git::catalog::label(scope.category) + "..." : !scope.extra.error.empty() ? scope.extra.error : canLoadMore ? "Load older entries" : "Result limit reached";
        if (button(ctx, mk(parent, 586005), preset::Button(text, !scope.extraFuture.valid() && (canLoadMore || !scope.extra.error.empty()))
            .with_size(ComponentSize{percent(1.f), pixels(26)}).with_font_size(pixels(12)).with_debug_name("quick_open_load_more"))) scope.moreRequested = true;
        height -= 26.f;
    }
    const auto listParent = mk(parent, 586003);
    const bool moving = pickerKeys && (afterhours::input::is_key_pressed(264) || afterhours::input::is_key_pressed(265));
    if (revealSelection || moving) {
        auto [entity, owner] = afterhours::ui::imm::deref(listParent);
        if (entity.has<afterhours::ui::HasScrollView>()) {
            auto& scroll = entity.get<afterhours::ui::HasScrollView>();
            const float rowHeight = 28.f * ui::zoom::get();
            const float top = static_cast<float>(layout.filePickerIndex) * rowHeight;
            const float viewport = std::max(28.f, height - 118.f) * ui::zoom::get();
            float target = scroll.scroll_offset.y;
            if (top < target) target = top;
            else if (top + rowHeight > target + viewport) target = top + rowHeight - viewport;
            scroll.scroll_offset.y = scroll.scroll_target.y = scroll.last_eased_offset.y = std::max(0.f, target);
            scroll.anchor_child = -1;
        }
        if (moving) ui::focus_control(ctx, input.ent());
    }
    ui::virtual_list(ctx, listParent, results.size(), 28.f,
        [&](size_t i, Entity& row) {
            auto spans = results[i].kind == git::catalog::Kind::File
                ? file_picker_label(results[i].title, position.parsed.path, static_cast<int>(i) == layout.filePickerIndex)
                : std::vector<afterhours::ui::TextSpan>{{git::catalog::label(results[i].kind) + " · ", theme::TEXT_SECONDARY},
                    {results[i].title + "  " + results[i].object.substr(0, 7), theme::TEXT_PRIMARY}};
            if (!results[i].detail.empty()) spans.push_back({"  · " + results[i].detail, theme::TEXT_SECONDARY});
            auto result = button(ctx, mk(row, 0), preset::Button(results[i].title)
                    .with_styled_label(spans)
                    .with_size(ComponentSize{percent(1.f), pixels(28)})
                    .with_alignment(TextAlignment::Left)
                    .with_custom_background(static_cast<int>(i) == layout.filePickerIndex ? theme::BUTTON_PRIMARY : theme::PANEL_BG)
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_font_size(pixels(14)).with_debug_name("file_picker_result"));
            if (results[i].kind == git::catalog::Kind::File)
                ui::set_truncated_tooltip(result.ent(), results[i].identity + "\n" + results[i].detail, result.ent());
            else ui::set_tooltip(result.ent(), results[i].identity + "\n" + results[i].detail);
            ui::bind_focus(result.ent(), repo, reading::focus::Region::Picker, results[i].kind == git::catalog::Kind::File ? results[i].identity : results[i].key());
            if (result) open(results[i], false);
            if (results[i].kind == git::catalog::Kind::Tag && ctx.is_right_click(result.ent().id)) {
                const auto targetRevision = reading::revision_text(reading::source_revision_for(repo.workspace().location()));
                const bool historical = std::holds_alternative<reading::ObjectId>(reading::source_revision_for(repo.workspace().location()));
                ui::show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, {ui::ContextMenuItem::item("Compare tag with current review", [base = results[i].object, targetRevision] {
                    if (auto* active = find_singleton<RepoComponent, ActiveTab>())
                        navigation::open(*active, reading::review("compare:" + base + ":" + targetRevision), true, reading::OpenMode::Keep);
                }, historical)});
            }
        }, ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(std::max(28.f, height - 118.f))})
            .with_debug_name("file_picker_list"));
}

struct FilePickerSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* layout = find_singleton<LayoutComponent>();
        auto* repo = find_singleton<RepoComponent, ActiveTab>();
        if (!layout) return;
        if (!repo || repo->repoPath.empty()) layout->filePickerOpen = false;
        if (layout->filePickerOpen && repo) {
            auto& scope = layout->filePickerScope;
            if (!scope.owner || scope.owner->repository != repo->repoPath ||
                    scope.owner->generation != repo->workspace().generation() ||
                    scope.owner->dataGeneration != repo->dataGeneration ||
                    !reading::same_document(scope.owner->document, repo->workspace().location())) {
                scope = {};
                scope.owner = navigation::stamp(*repo, {});
                scope.documentRevision = reading::source_revision_for(repo->workspace().location());
                layout->filePickerCacheKey.clear();
            }
        }
        auto& root = ui_imm::getUIRootEntity();
        const float zoom = ui::zoom::get();
        const float screenWidth = ctx.screen_width / zoom;
        const float screenHeight = ctx.screen_height / zoom;
        const float width = std::max(100.f, std::min(620.f, screenWidth - 32.f));
        const float height = layout->filePickerPosition.lineMode ? 204.f : std::max(160.f, std::min(440.f, screenHeight - 80.f));
        auto modal = afterhours::modal::detail::modal_impl(ctx, mk(root, 586100), layout->filePickerOpen,
            afterhours::ModalConfig{}.with_size(pixels(width), pixels(height))
                .with_show_close_button(false).with_closed_by(afterhours::ClosedBy::Any)
                .with_backdrop_color({0, 0, 0, 0}));
        modal.ent().get<afterhours::modal::Modal>().previously_focused_element = -1;
        if (!modal || !repo) {
            if (layout->filePickerScope.owner) {
                layout->filePickerScope = {};
                layout->filePickerPosition = {};
                layout->filePickerResults.clear();
                layout->filePickerCacheKey.clear();
            }
            ctx.remove_input_gate("file_picker");
            return;
        }
        modal.cmp().absolute_pos_x = (ctx.screen_width - width * zoom) * .5f;
        modal.cmp().absolute_pos_y = (ctx.screen_height - height * zoom) * .3f;
        div(ctx, mk(root, 586101), ComponentConfig{}
            .with_size(ComponentSize{pixels(screenWidth), pixels(screenHeight)})
            .with_absolute_position().with_custom_background(afterhours::Color{0, 0, 0, 64})
            .with_roundness(0.f).with_render_layer(998).with_debug_name("file_picker_backdrop"));
        auto body = div(ctx, mk(modal.ent(), 586102), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), expand()})
            .with_flex_direction(FlexDirection::Column).with_no_wrap()
            .with_render_layer(1001).with_debug_name("file_picker_overlay"));
        const auto modalId = modal.ent().id;
        ctx.add_input_gate("file_picker", [modalId](afterhours::EntityID id) {
            return afterhours::modal::detail::is_entity_in_tree(modalId, id);
        });
        if (layout->filePickerPosition.lineMode) render_line_picker(ctx, body.ent(), *repo, *layout);
        else render_file_picker(ctx, body.ent(), *repo, *layout, height - 48.f);
    }
};

}
