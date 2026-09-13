#pragma once

#include "../settings.h"
#include "context_menu.h"
#include "diff_renderer.h"
#include "file_history.h"
#include "../util/markdown_preview.h"

namespace ecs {

inline bool render_source_header(UIContext<InputAction>& ctx, Entity& parent,
                                 RepoComponent& repo, const LayoutComponent& layout) {
    const bool compact = layout.mainContent.width < 450.f;
    auto header = div(ctx, mk(parent, 585000), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(32)}).with_skip_grid_snap()
        .with_flex_direction(FlexDirection::Row).with_no_wrap()
        .with_align_items(AlignItems::Center).with_gap(pixels(4))
        .with_debug_name("full_file_header"));
    auto back = button(ctx, mk(header.ent(), 0), preset::Button(compact ? "<" : "< Review")
        .with_size(ComponentSize{pixels(compact ? 28 : 84), pixels(28)})
        .with_font_size(pixels(12)).with_debug_name("full_file_back"));
    ui::set_tooltip(back.ent(), "Return to review");
    if (back) {
        navigation::return_to_review(repo);
        return true;
    }
    auto path = div(ctx, mk(header.ent(), 1), ComponentConfig{}
        .with_label(repo.fullFilePath()).with_size(ComponentSize{expand(), pixels(28)})
        .with_font_size(pixels(12)).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
        .with_debug_name("full_file_path"));
    ui::set_tooltip(path.ent(), repo.fullFilePath());
    const auto& revision = repo.fullFileRevision();
    const auto badge = revision.empty() ? (compact ? "WT" : "Working tree") :
        revision == "INDEX" ? "Index" : revision.substr(0, 7);
    auto revisionLabel = div(ctx, mk(header.ent(), 2), ComponentConfig{}
        .with_label(badge).with_size(ComponentSize{pixels(compact ? 58 : 94), pixels(28)})
        .with_font_size(pixels(12)).with_custom_text_color(theme::TEXT_SECONDARY)
        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
        .with_debug_name("full_file_revision"));
    ui::set_tooltip(revisionLabel.ent(), revision.empty() ? "Working tree" : revision);
    auto options = button(ctx, mk(header.ent(), 3), preset::Button("...")
        .with_size(ComponentSize{pixels(28), pixels(28)}).with_font_size(pixels(12))
        .with_debug_name("full_file_options"));
    ui::set_tooltip(options.ent(), "File actions");
    if (!options) return false;
    ui::remember_focus_origin(ctx, options.ent());
    const auto request = navigation::stamp(repo, "source-header");
    const auto current = [request]() -> RepoComponent* {
        auto* active = find_singleton<RepoComponent, ActiveTab>();
        return active && navigation::accepts(*active, request, request.key) ? active : nullptr;
    };
    const auto rect = ui::screen_rect(options.ent());
    const float x = rect.x, y = rect.y + rect.height;
    std::vector<ui::ContextMenuItem> items;
    items.push_back(ui::ContextMenuItem::item("File history", [current] {
        if (auto* active = current()) open_file_history(*active, active->fullFilePath(), active->fullFileRevision());
    }));
    const auto* document = repo.workspace().document(repo.workspace().active_id());
    const auto& selection = document->selection;
    const int selectedLine = selection && selection->anchor.path == repo.fullFilePath() && selection->anchor != selection->head
        ? selection->anchor.line : 0;
    const int bookmarkLine = selectedLine > 0 ? selectedLine : document->caret ? document->caret->line :
        document->anchor ? document->anchor->line : std::max(1, repo.fullFileTargetLine());
    const auto range = std::find_if(repo.sourceFoldRanges.rbegin(), repo.sourceFoldRanges.rend(), [&](auto fold) {
        return fold.first == bookmarkLine || fold.contains(bookmarkLine);
    });
    if (range != repo.sourceFoldRanges.rend()) {
        const auto fold = *range;
        const auto identity = repo.sourceFoldIdentity;
        items.push_back(ui::ContextMenuItem::item(document->sourceFolds.closed(fold) ? "Unfold block at caret" : "Fold block at caret",
            [current, fold, identity] {
                if (auto* active = current(); active && active->sourceFoldIdentity == identity) navigation::toggle_source_fold(*active, fold);
            }, document->sourceFolds.can_toggle(fold), document->sourceFolds.can_toggle(fold) ? "" : "Limit reached"));
    }
    if (!document->sourceFolds.folded.empty()) items.push_back(ui::ContextMenuItem::item("Unfold all", [current] {
        if (auto* active = current()) navigation::unfold_source(*active);
    }));
    const auto sameBookmark = [path = repo.fullFilePath(), revision, bookmarkLine](const CodeBookmark& bookmark) {
        return bookmark.path == path && bookmark.revision == revision && bookmark.line == bookmarkLine;
    };
    const auto& bookmarks = Settings::get().get_code_bookmarks(repo.repoPath);
    const bool bookmarked = std::any_of(bookmarks.begin(), bookmarks.end(), sameBookmark);
    items.push_back(ui::ContextMenuItem::item(bookmarked ? "Remove bookmark" : "Bookmark line " + std::to_string(bookmarkLine),
        [current, sameBookmark, bookmarkLine] {
            if (auto* active = current()) {
                auto updated = Settings::get().get_code_bookmarks(active->repoPath);
                if (std::any_of(updated.begin(), updated.end(), sameBookmark)) std::erase_if(updated, sameBookmark);
                else updated.push_back({active->fullFilePath(), active->fullFileRevision(), bookmarkLine,
                    active->fullFilePath() + ":L" + std::to_string(bookmarkLine)});
                Settings::get().set_code_bookmarks(active->repoPath, updated);
            }
        }));
    items.push_back(ui::ContextMenuItem::item("Bookmarks", [current, x, y] {
        auto* active = current();
        if (!active) return;
        std::vector<ui::ContextMenuItem> entries;
        for (const auto& bookmark : Settings::get().get_code_bookmarks(active->repoPath)) {
            entries.push_back(ui::ContextMenuItem::item(bookmark_display(bookmark), [current, bookmark] {
                if (auto* source = current())
                    navigation::open(*source, reading::source(bookmark.path, bookmark.revision, bookmark.line));
            }));
        }
        if (entries.empty()) entries.push_back(ui::ContextMenuItem::item("No bookmarks", [] {}, false));
        ui::show_context_menu(x, y, std::move(entries));
    }, true, std::to_string(bookmarks.size())));
    items.push_back(ui::ContextMenuItem::item(selectedLine > 0 ? "Blame line " + std::to_string(selectedLine) : "Blame selected line",
        [current, selectedLine] {
            if (auto* active = current()) {
                std::vector<std::string> args{"blame", "--line-porcelain", "-L", std::to_string(selectedLine) + "," + std::to_string(selectedLine)};
                if (!active->fullFileRevision().empty()) args.push_back(active->fullFileRevision());
                args.insert(args.end(), {"--", active->fullFilePath()});
                active->blameFutureStamp = navigation::stamp(*active, std::to_string(selectedLine));
                active->blameFuture = git::git_run_async(active->repoPath, args);
                active->blameLine = {};
                active->blameError.clear();
                active->blameOpen = true;
            }
        }, selectedLine > 0 && revision != "INDEX"));
    const bool markdown = markdown_preview::is_markdown_path(repo.fullFilePath()) &&
        !repo.fullFileDiff.empty() && !repo.fullFileDiff.front().isBinary;
    if (markdown) items.push_back(ui::ContextMenuItem::item(repo.fullFileMarkdownPreview ? "Raw Markdown" : "Preview Markdown", [current] {
        if (auto* active = current()) active->fullFileMarkdownPreview = !active->fullFileMarkdownPreview;
    }));
    items.push_back(ui::ContextMenuItem::separator());
    items.push_back(ui::ContextMenuItem::item("Reload file", [current] {
        if (auto* active = current()) {
            navigation::set_selection(*active, {});
            navigation::release_source(*active);
            navigation::restore_anchor(*active);
        }
    }));
    items.push_back(ui::ContextMenuItem::item("Encoding", [current, x, y] {
        auto* active = current();
        if (!active) return;
        std::vector<ui::ContextMenuItem> choices;
        for (const auto& [label, encoding] : std::vector<std::pair<std::string, std::string>>{
                {"Auto detect", "auto"}, {"UTF-8", "utf8"}, {"UTF-16 LE", "utf16le"}, {"UTF-16 BE", "utf16be"}}) {
            choices.push_back(ui::ContextMenuItem::item(label, [current, encoding] {
                if (auto* source = current(); source && source->fullFileEncodingOverride != encoding) {
                    source->fullFileEncodingOverride = encoding;
                    source->fullFileCacheKey.clear();
                    source->fullFilePage = {};
                    source->fullFilePageRequest = {};
                    navigation::clear_source_reveal(*source);
                    source->fullFileRequestedTargetLine = source->fullFileRequestedTargetColumn = 0;
                }
            }, active->fullFileEncodingOverride != encoding,
                active->fullFileEncodingOverride == encoding ? "Selected" : ""));
        }
        ui::show_context_menu(x, y, std::move(choices));
    }, true, repo.fullFileEncodingLabel));
    ui::show_context_menu(x, y, std::move(items));
    return false;
}

}
