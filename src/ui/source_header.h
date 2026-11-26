#pragma once

#include "../settings.h"
#include "context_menu.h"
#include "diff_renderer.h"
#include "file_picker.h"
#include "file_history.h"
#include "source_edit.h"
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
    auto back = button(ctx, mk(header.ent(), 0), preset::Button("< Review")
        .with_size(ComponentSize{pixels(84), pixels(28)})
        .with_font_size(pixels(12)).with_debug_name("full_file_back"));
    ui::set_tooltip(back.ent(), "Return to review");
    if (back) {
        navigation::return_to_review(repo);
        return true;
    }
    // Clickable breadcrumbs: path segments open Quick Open filtered to
    // that directory; the enclosing symbol opens the outline picker.
    {
        auto crumbs = div(ctx, mk(header.ent(), 1), ComponentConfig{}
            .with_size(ComponentSize{expand(), pixels(28)}).with_flex_direction(FlexDirection::Row)
            .with_no_wrap().with_align_items(AlignItems::Center).with_debug_name("full_file_path"));
        const auto fullPath = repo.fullFilePath();
        std::vector<std::string> segments;
        for (size_t at = 0, next; at <= fullPath.size(); at = next + 1) {
            next = fullPath.find('/', at);
            if (next == std::string::npos) next = fullPath.size();
            segments.push_back(fullPath.substr(at, next - at));
            if (next == fullPath.size()) break;
        }
        std::string prefix;
        for (size_t i = 0; i < segments.size(); ++i) {
            prefix += (i ? "/" : "") + segments[i];
            const bool last = i + 1 == segments.size();
            auto segment = button(ctx, mk(crumbs.ent(), static_cast<int>(10 + i)), preset::Button(segments[i])
                .with_size(ComponentSize{children(), pixels(24)}).with_font_size(pixels(12))
                .with_debug_name("breadcrumb_" + std::to_string(i)));
            ui::set_tooltip(segment.ent(), last ? std::string("Open outline") : "Open Quick Open at " + prefix);
            if (!last) div(ctx, mk(crumbs.ent(), static_cast<int>(50 + i)), ComponentConfig{}
                .with_label("/").with_size(ComponentSize{children(), pixels(24)})
                .with_font_size(pixels(12)).with_custom_text_color(theme::TEXT_TERTIARY)
                .with_debug_name("breadcrumb_sep_" + std::to_string(i)));
            if (segment) {
                if (auto* layout = find_singleton<LayoutComponent>()) {
                    if (last) open_symbol_picker(repo, *layout);
                    else {
                        layout->filePickerPosition = {};
                        layout->filePickerQuery = prefix + "/";
                        layout->open_picker(PickerMode::Files, repo);
                    }
                }
            }
        }
        const auto* document = repo.workspace().document(repo.workspace().active_id());
        const int cursorLine = document && document->caret ? document->caret->line :
            document && document->anchor ? document->anchor->line : 0;
        // Symbol text comes from the full-file diff (decodedText is only
        // kept for markdown); strip the leading diff marker per line.
        static std::string symbolTextKey, symbolTextCache;
        std::string symbolText;
        if (document && cursorLine > 0 && !repo.fullFileDiff.empty()) {
            size_t lines = 0;
            for (const auto& file : repo.fullFileDiff) for (const auto& hunk : file.hunks) lines += hunk.lines.size();
            if (lines <= 5000) {
                const auto key = fullPath + "\n" + std::to_string(repo.dataGeneration) + ":" + std::to_string(lines);
                if (key != symbolTextKey) {
                    symbolTextCache.clear();
                    for (const auto& file : repo.fullFileDiff)
                        for (const auto& hunk : file.hunks)
                            for (const auto& line : hunk.lines) { symbolTextCache += line.substr(1); symbolTextCache += '\n'; }
                    symbolTextKey = key;
                }
                symbolText = symbolTextCache;
            }
        }
        if (document && cursorLine > 0 && !symbolText.empty()) {
            const auto& symbols = symbol_outline::cached_scan(fullPath, symbolText);
            const symbol_outline::Symbol* enclosing = nullptr;
            for (const auto& symbol : symbols) if (symbol.line <= cursorLine) enclosing = &symbol;
            if (enclosing) {
                auto symbol = button(ctx, mk(crumbs.ent(), 90), preset::Button("› " + enclosing->name)
                    .with_size(ComponentSize{children(), pixels(24)}).with_font_size(pixels(12))
                    .with_debug_name("breadcrumb_symbol"));
                ui::set_tooltip(symbol.ent(), "Open outline");
                if (symbol) if (auto* layout = find_singleton<LayoutComponent>()) open_symbol_picker(repo, *layout);
            }
        }
        ui::set_truncated_tooltip(crumbs.ent(), fullPath, crumbs.ent());
    }
    const auto& revision = repo.fullFileRevision();
    const auto badge = revision.empty() ? (compact ? "WT" : "Working tree") :
        revision == "INDEX" ? "Index" : revision.substr(0, 7);
    auto revisionLabel = div(ctx, mk(header.ent(), 2), ComponentConfig{}
        .with_label(badge).with_size(ComponentSize{children(), pixels(28)})
        .with_font_size(pixels(12)).with_custom_text_color(theme::TEXT_SECONDARY)
        .with_transparent_bg()
        .with_padding(Padding{.left = pixels(4), .right = pixels(4)})
        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
        .with_debug_name("full_file_revision"));
    ui::set_tooltip(revisionLabel.ent(), revision.empty() ? "Working tree" : revision);
    // Read-only / Editing pill: working-tree files can switch into an
    // editable buffer; committed and index content stays read-only.
    if (source_edit_eligible(repo)) {
        const auto* doc = repo.workspace().document(repo.workspace().active_id());
        const bool editing = doc && doc->edit_mode();
        const bool dirty = doc && doc->edit_dirty();
        const bool saveFailed = doc && doc->edit == reading::Edit::SaveFailed;
        auto pill = button(ctx, mk(header.ent(), 4), preset::Button(saveFailed ? "Save failed" : editing ? (dirty ? "Editing •" : "Editing") : "Read-only")
            .with_size(ComponentSize{pixels(compact ? 62 : 84), pixels(28)})
            .with_custom_background(editing ? theme::SEGMENT_SELECTED_BG : theme::BUTTON_SECONDARY)
            .with_custom_text_color(saveFailed ? theme::DESTRUCTIVE_TEXT : editing ? theme::TEXT_PRIMARY : theme::TEXT_SECONDARY)
            .with_border(editing ? theme::SELECTED_ACCENT : theme::BORDER, pixels(1))
            .with_font_size(pixels(12)).with_debug_name("source_edit_toggle"));
        ui::set_tooltip(pill.ent(), saveFailed && doc ? doc->editError : editing ? "Switch back to read-only (unsaved changes are kept in the buffer)" : "Edit this file");
        if (pill) {
            if (auto* editDoc = navigation::active_edit_document(repo)) {
                if (editing) editDoc->stop_edit();
                else editDoc->begin_edit();
            }
        }
    } else {
        auto pill = div(ctx, mk(header.ent(), 4), ComponentConfig{}
            .with_label("Read-only").with_size(ComponentSize{pixels(compact ? 62 : 84), pixels(28)})
            .with_font_size(pixels(12)).with_custom_text_color(theme::TEXT_SECONDARY)
            .with_debug_name("source_readonly_pill"));
        ui::set_tooltip(pill.ent(), revision == "INDEX" ? "Index content is read-only" : "Committed content is read-only");
    }
    {
        auto* mutableLayout = find_singleton<LayoutComponent>();
        auto outline = button(ctx, mk(header.ent(), 5), preset::Button("Outline")
            .with_size(ComponentSize{pixels(64), pixels(28)}).with_font_size(pixels(12))
            .with_custom_background(mutableLayout && mutableLayout->outlinePanelOpen ? theme::SELECTED_BG : theme::PANEL_BG)
            .with_debug_name("outline_toggle"));
        ui::set_tooltip(outline.ent(), "Toggle the persistent outline panel");
        if (outline && mutableLayout) mutableLayout->outlinePanelOpen = !mutableLayout->outlinePanelOpen;
    }
    auto options = button(ctx, mk(header.ent(), 3), preset::Button("...")
        .with_size(ComponentSize{pixels(36), pixels(28)}).with_font_size(pixels(12))
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
    items.push_back(ui::ContextMenuItem::item("Minimap (toggle)", [current] {
        if (auto* active = current())
            if (auto* l = ecs::find_singleton<ecs::LayoutComponent>()) l->minimap = !l->minimap;
    }));
    items.push_back(ui::ContextMenuItem::item("Blame Gutter (toggle)", [current] {
        if (auto* active = current())
            if (auto* l = ecs::find_singleton<ecs::LayoutComponent>()) l->blameGutter = !l->blameGutter;
    }));
    items.push_back(ui::ContextMenuItem::item(selectedLine > 0 ? "Blame line " + std::to_string(selectedLine) : "Blame selected line",
        [current, selectedLine] {
            if (auto* active = current()) {
                std::vector<std::string> args{"blame", "--line-porcelain", "-L", std::to_string(selectedLine) + "," + std::to_string(selectedLine)};
                if (!active->fullFileRevision().empty()) args.push_back(active->fullFileRevision());
                args.insert(args.end(), {"--", active->fullFilePath()});
                active->blameLine = {};
                active->blame.request(std::to_string(selectedLine),
                    navigation::stamp(*active, std::to_string(selectedLine)),
                    git::git_run_async(active->repoPath, args));
                active->blameOpen = true;
            }
        }, selectedLine > 0 && revision != "INDEX"));
    const bool markdown = markdown_preview::is_markdown_path(repo.fullFilePath()) &&
        !repo.fullFileDiff.empty() && !repo.fullFileDiff.front().isBinary;
    if (markdown) items.push_back(ui::ContextMenuItem::item(repo.fullFileMarkdownPreview ? "Raw Markdown" : "Preview Markdown", [current] {
        if (auto* active = current()) active->fullFileMarkdownPreview = !active->fullFileMarkdownPreview;
    }));
    if (source_edit_eligible(repo)) {
        items.push_back(ui::ContextMenuItem::separator());
        if (!document->edit_mode()) {
            items.push_back(ui::ContextMenuItem::item("Edit file", [current] {
                if (auto* active = current())
                    if (auto* doc = navigation::active_edit_document(*active)) doc->begin_edit();
            }));
        } else {
            items.push_back(ui::ContextMenuItem::item("Save", [current] {
                if (auto* active = current())
                    if (auto* doc = navigation::active_edit_document(*active)) save_edit_buffer(*active, *doc);
            }, document->edit_dirty(), document->edit_dirty() ? "" : "No unsaved changes"));
            items.push_back(ui::ContextMenuItem::item("Discard changes", [current] {
                if (auto* active = current())
                    if (auto* doc = navigation::active_edit_document(*active)) discard_edit_buffer(*doc);
            }, document->edit_dirty(), document->edit_dirty() ? "" : "No unsaved changes"));
            items.push_back(ui::ContextMenuItem::item("Stop editing", [current] {
                if (auto* active = current())
                    if (auto* doc = navigation::active_edit_document(*active)) doc->stop_edit();
            }));
        }
    }
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
                    source->fullFileLoad.invalidate_key();
                    source->fullFilePage = {};
                    source->fullFileLoad.request = {};
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
