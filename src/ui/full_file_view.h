#pragma once

#include <algorithm>
#include "../settings.h"
#include "diff_renderer.h"
#include "file_history.h"
#include "../git/content_reader.h"
#include "../util/hex_view.h"
#include "../util/markdown_preview.h"
#include "../util/text_decode.h"

namespace ecs {

inline std::string bookmark_display(const CodeBookmark& bookmark) {
    std::string label = bookmark.label.empty()
        ? bookmark.path + ":L" + std::to_string(bookmark.line)
        : bookmark.label;
    if (!bookmark.revision.empty()) label += " @ " + bookmark.revision.substr(0, std::min<size_t>(7, bookmark.revision.size()));
    return label;
}

inline void render_full_file(UIContext<InputAction>& ctx, Entity& parent,
                             RepoComponent& repo, LayoutComponent& layout) {
    const auto& bookmarks = Settings::get().get_code_bookmarks(repo.repoPath);
    if (repo.fullFileNavigateFrames > 0) repo.fullFileMarkdownPreview = false;
    std::string sourceKey = repo.repoPath + "\n" + repo.fullFileRevision() + "\n" + repo.fullFilePath();
    if (repo.fullFileRevision().empty() || repo.fullFileRevision() == "INDEX") sourceKey += ":" + std::to_string(repo.dataGeneration);
    if (repo.fullFileSourceKey != sourceKey) {
        repo.fullFileSourceKey = sourceKey;
        repo.fullFilePage = {};
        repo.fullFilePageRequest = {};
        repo.fullFileRequestedTargetLine = 0;
    }
    if (repo.fullFileTargetLine() > 0 && repo.fullFileRequestedTargetLine != repo.fullFileTargetLine()) {
        repo.fullFileRequestedTargetLine = repo.fullFileTargetLine();
        int lastLine = repo.fullFilePage.next.line - (repo.fullFilePage.next.continuation ? 0 : 1);
        if (repo.fullFileDiff.empty() || repo.fullFileTargetLine() < repo.fullFilePage.begin.line || repo.fullFileTargetLine() > lastLine) {
            const auto* document = repo.workspace().document(repo.workspace().active_id());
            const int leading = document->restoreAnchor && document->anchor
                ? static_cast<int>(std::ceil(layout.mainContent.height * document->anchor->viewportFraction /
                    (Settings::get().get_code_font_size() + 8.f))) + 1 : 0;
            repo.fullFilePageRequest = {FilePageRequest::Action::TargetLine, {}, repo.fullFileTargetLine(), {}, leading};
        }
    }
    const auto& pageRequest = repo.fullFilePageRequest;
    std::string key = sourceKey + "\n" + repo.fullFileEncodingOverride + ":" +
        std::to_string(static_cast<int>(pageRequest.action)) + ":" + std::to_string(pageRequest.cursor.offset) + ":" + std::to_string(pageRequest.targetLine) + ":" + std::to_string(pageRequest.leadingLines);
    bool changed = repo.fullFileCacheKey != key;
    if (changed) {
        repo.fullFileCacheKey = key;
        repo.fullFileDiff.clear();
        repo.fullFileError.clear();
        repo.fullFileBytes.clear();
        repo.fullFileDecodedText.clear();
        repo.blameOpen = false;
        repo.blameFuture = {};
        repo.fullFileRequestStamp = navigation::stamp(repo, key);
        repo.fullFileFuture = git::read_file_async({repo.repoPath, repo.fullFilePath(), repo.fullFileRevision(),
            pageRequest, repo.fullFileEncodingOverride, repo.fullFilePage.encoding});
    }
    if (repo.fullFileFuture.valid() && repo.fullFileFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto content = repo.fullFileFuture.get();
        if (!navigation::accepts(repo, repo.fullFileRequestStamp, key)) {
            repo.fullFileCacheKey.clear();
            return;
        }
        if (navigation::resolve_source(repo, repo.fullFileRequestStamp, content.resolvedRevision)) {
            auto resolvedKey = repo.repoPath + "\n" + repo.fullFileRevision() + "\n" + repo.fullFilePath();
            repo.fullFileCacheKey.replace(0, sourceKey.size(), resolvedKey);
            repo.fullFileSourceKey = resolvedKey;
        }
        repo.fullFileError = std::move(content.error);
        repo.fullFileBytes = std::move(content.raw);
        repo.fullFilePage = std::move(content.page);
        repo.fullFileEncodingLabel = std::move(content.encodingLabel);
        repo.fullFileDecodedText = std::move(content.decodedText);
        if (repo.fullFileError.empty()) repo.fullFileDiff.push_back(std::move(content.diff));
        changed = true;
    }
    auto header = div(ctx, mk(parent, 585000), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(34)})
        .with_flex_direction(FlexDirection::Row).with_debug_name("full_file_header"));
    if (button(ctx, mk(header.ent(), 0), preset::Button("Back to diff")
            .with_size(ComponentSize{pixels(110), pixels(30)}).with_debug_name("full_file_back"))) {
        navigation::return_to_review(repo);
    }
    div(ctx, mk(header.ent(), 1), ComponentConfig{}
        .with_label(repo.fullFilePath() + " @ " + (repo.fullFileRevision().empty() ? "working tree" : repo.fullFileRevision()))
        .with_size(ComponentSize{expand(), pixels(30)}).with_font_size(pixels(12))
        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
        .with_debug_name("full_file_revision"));
    if (button(ctx, mk(header.ent(), 2), preset::Button("History")
            .with_size(ComponentSize{pixels(75), pixels(30)}).with_debug_name("file_history_open")))
        open_file_history(repo, repo.fullFilePath(), repo.fullFileRevision());
    constexpr float headerHeight = 66.f;
    auto actions = div(ctx, mk(parent, 585010), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(32)})
        .with_flex_direction(FlexDirection::Row).with_debug_name("full_file_actions"));
    if (button(ctx, mk(actions.ent(), 5), preset::Button(text_decode::override_label(repo.fullFileEncodingOverride, repo.fullFileEncodingLabel))
            .with_size(ComponentSize{expand(), pixels(30)})
            .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("encoding_cycle"))) {
        repo.fullFileEncodingOverride = text_decode::next_override(repo.fullFileEncodingOverride);
        repo.fullFileCacheKey.clear();
        repo.fullFilePage = {};
        repo.fullFilePageRequest = {};
        navigation::clear_source_reveal(repo);
        repo.fullFileRequestedTargetLine = 0;
    }
    const auto& page = repo.fullFilePage;
    bool partial = page.begin.offset != 0 || page.next.offset < page.totalBytes;
    float pageHeight = partial || repo.fullFileFuture.valid() || !repo.fullFileError.empty() ? 64.f : 0.f;
    if (pageHeight > 0) {
        auto controls = div(ctx, mk(parent, 585020), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(32)}).with_flex_direction(FlexDirection::Row));
        auto requestPage = [&](FilePageRequest request) {
            navigation::cancel_anchor(repo);
            repo.reading = {};
            repo.fullFilePageRequest = std::move(request);
            navigation::clear_source_reveal(repo);
            repo.fullFileRequestedTargetLine = 0;
            repo.fullFileNavigateFrames = 0;
            repo.fullFileCacheKey.clear();
            repo.fullFileDiff.clear();
            repo.fullFileDecodedText.clear();
        };
        if (button(ctx, mk(controls.ent(), 0), preset::Button("Previous page")
                .with_size(ComponentSize{expand(), pixels(30)}).with_disabled(page.begin.offset == 0 || repo.fullFileFuture.valid())
                .with_debug_name("file_page_previous")))
            requestPage({FilePageRequest::Action::Previous, page.begin, 0, page.sourceIdentity});
        if (button(ctx, mk(controls.ent(), 1), preset::Button("Next page")
                .with_size(ComponentSize{expand(), pixels(30)}).with_disabled(page.next.offset >= page.totalBytes || repo.fullFileFuture.valid())
                .with_debug_name("file_page_next")))
            requestPage({FilePageRequest::Action::Next, page.next, 0, page.sourceIdentity});
        if (button(ctx, mk(controls.ent(), 2), preset::Button("Reload from start")
                .with_size(ComponentSize{expand(), pixels(30)}).with_debug_name("file_page_start"))) {
            repo.fullFilePage.encoding.clear();
            requestPage({});
        }
        auto range = repo.fullFileFuture.valid() ? "Loading bounded file page..." :
            "Loaded lines " + std::to_string(page.begin.line) + "–" + std::to_string(page.next.line - (page.next.continuation ? 0 : 1)) +
            " · bytes " + std::to_string(page.begin.offset) + "–" + std::to_string(page.next.offset) + " of " + std::to_string(page.totalBytes) +
            (page.begin.continuation || (page.next.offset < page.totalBytes && page.next.continuation) ? " · line fragment" : "") + " · Find and Copy cover this page only";
        div(ctx, mk(parent, 585021), ComponentConfig{}.with_label(range)
            .with_size(ComponentSize{percent(1.f), pixels(32)}).with_font_size(pixels(12))
            .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("file_page_range"));
    }
    bool markdown = markdown_preview::is_markdown_path(repo.fullFilePath()) &&
                    !repo.fullFileDiff.empty() && !repo.fullFileDiff.front().isBinary;
    if (markdown && button(ctx, mk(actions.ent(), 6), preset::Button(repo.fullFileMarkdownPreview ? "Raw Markdown" : "Preview")
            .with_size(ComponentSize{expand(), pixels(30)}).with_debug_name("markdown_preview_toggle")))
        repo.fullFileMarkdownPreview = !repo.fullFileMarkdownPreview;
    const auto& selection = ui::diff_sel::state();
    int selectedLine = 0;
    for (const auto& line : selection.lastLines)
        if (line.ent == selection.anchor.ent && line.filePath == repo.fullFilePath()) selectedLine = line.lineNo;
    int bookmarkLine = selectedLine > 0 ? selectedLine : std::max(1, repo.fullFileTargetLine());
    auto sameBookmark = [&](const CodeBookmark& bookmark) {
        return bookmark.path == repo.fullFilePath() &&
               bookmark.revision == repo.fullFileRevision() &&
               bookmark.line == bookmarkLine;
    };
    bool bookmarked = std::any_of(bookmarks.begin(), bookmarks.end(), sameBookmark);
    if (button(ctx, mk(actions.ent(), 4), preset::Button(bookmarked ? "Remove bookmark" : "Bookmark line " + std::to_string(bookmarkLine))
            .with_size(ComponentSize{expand(), pixels(30)}).with_font_size(pixels(12))
            .with_debug_name("bookmark_line"))) {
        auto updated = bookmarks;
        if (bookmarked) {
            std::erase_if(updated, sameBookmark);
        } else {
            updated.push_back(CodeBookmark{
                repo.fullFilePath(),
                repo.fullFileRevision(),
                bookmarkLine,
                repo.fullFilePath() + ":L" + std::to_string(bookmarkLine),
            });
        }
        Settings::get().set_code_bookmarks(repo.repoPath, updated);
    }
    if (selectedLine > 0 && repo.fullFileRevision() != "INDEX") {
        if (button(ctx, mk(actions.ent(), 3), preset::Button("Blame line " + std::to_string(selectedLine))
                .with_size(ComponentSize{expand(), pixels(30)}).with_debug_name("blame_selected_line"))) {
            std::vector<std::string> args{"blame", "--line-porcelain", "-L", std::to_string(selectedLine) + "," + std::to_string(selectedLine)};
            if (!repo.fullFileRevision().empty()) args.push_back(repo.fullFileRevision());
            args.insert(args.end(), {"--", repo.fullFilePath()});
            repo.blameFutureStamp = navigation::stamp(repo, std::to_string(selectedLine));
            repo.blameFuture = git::git_run_async(repo.repoPath, args);
            repo.blameLine = {};
            repo.blameError.clear();
            repo.blameOpen = true;
        }
    }
    float bookmarkHeight = bookmarks.empty() ? 0.f : 30.f;
    if (!bookmarks.empty()) {
        constexpr size_t pageSize = 3;
        size_t pageCount = (bookmarks.size() + pageSize - 1) / pageSize;
        repo.bookmarkPage = std::min(repo.bookmarkPage, pageCount - 1);
        auto row = div(ctx, mk(parent, 585009), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(bookmarkHeight)})
            .with_flex_direction(FlexDirection::Row)
            .with_align_items(AlignItems::Center)
            .with_gap(pixels(6))
            .with_padding(Padding{.left = pixels(8), .right = pixels(8)})
            .with_custom_background(theme::SECTION_HEADER_BG)
            .with_debug_name("bookmarks_row"));
        div(ctx, mk(row.ent(), 0), ComponentConfig{}
            .with_label("Bookmarks")
            .with_size(ComponentSize{pixels(78), pixels(24)})
            .with_font_size(pixels(12))
            .with_custom_text_color(theme::TEXT_SECONDARY)
            .with_debug_name("bookmarks_label"));
        if (button(ctx, mk(row.ent(), 1), preset::Button("<")
                .with_size(ComponentSize{pixels(28), pixels(24)})
                .with_disabled(repo.bookmarkPage == 0).with_debug_name("bookmarks_previous"))) --repo.bookmarkPage;
        if (button(ctx, mk(row.ent(), 2), preset::Button(">")
                .with_size(ComponentSize{pixels(28), pixels(24)})
                .with_disabled(repo.bookmarkPage + 1 >= pageCount).with_debug_name("bookmarks_next"))) ++repo.bookmarkPage;
        for (size_t i = repo.bookmarkPage * pageSize; i < std::min(bookmarks.size(), (repo.bookmarkPage + 1) * pageSize); ++i) {
            const auto& bookmark = bookmarks[i];
            auto label = bookmark_display(bookmark);
            auto item = button(ctx, mk(row.ent(), static_cast<int>(i % pageSize) + 3), preset::Button(label)
                    .with_size(ComponentSize{expand(), pixels(24)})
                    .with_font_size(pixels(12))
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_debug_name("code_bookmark"));
            ui::set_tooltip(item.ent(), label);
            if (item) {
                navigation::open(repo, reading::source(bookmark.path, bookmark.revision, bookmark.line));
            }
        }
    }
    if (repo.blameFuture.valid() && repo.blameFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = repo.blameFuture.get();
        if (!navigation::accepts(repo, repo.blameFutureStamp, repo.blameFutureStamp.key)) return;
        repo.blameFuture = {};
        if (result.success()) {
            repo.blameLine = git::parse_blame_line(result.stdout_str());
            if (repo.blameLine.hash.empty()) repo.blameError = "Unable to read line attribution";
        } else repo.blameError = result.stderr_str();
    }
    float blameHeight = 0.f;
    if (repo.blameOpen) {
        blameHeight = 84.f;
        std::string label = "Loading line attribution...";
        if (!repo.blameLine.hash.empty()) {
            const auto& line = repo.blameLine;
            label = line.hash == std::string(40, '0') ? "Not committed yet" : line.hash + " · " + line.author + "\n" + line.summary;
            label += "\n" + line.file + ":" + std::to_string(line.originalLine) + " → line " + std::to_string(line.finalLine);
        }
        if (!repo.blameError.empty()) label = repo.blameError;
        auto panel = div(ctx, mk(parent, 585002), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(blameHeight)}).with_flex_direction(FlexDirection::Row)
            .with_custom_background(theme::PANEL_BG));
        div(ctx, mk(panel.ent(), 0), ComponentConfig{}.with_label(label)
            .with_size(ComponentSize{expand(), pixels(blameHeight)}).with_font_size(pixels(12))
            .with_text_overflow(afterhours::ui::TextOverflow::Wrap).with_debug_name("blame_attribution"));
        if (button(ctx, mk(panel.ent(), 1), preset::Button("Close")
                .with_size(ComponentSize{pixels(65), pixels(30)}))) repo.blameOpen = false;
    }
    if (repo.fullFileFuture.valid()) {
        div(ctx, mk(parent, 585003), ComponentConfig{}
            .with_label("Loading file...").with_size(ComponentSize{percent(1.f), pixels(40)})
            .with_font_size(pixels(14)).with_debug_name("full_file_loading"));
    } else if (!repo.fullFileError.empty()) {
        div(ctx, mk(parent, 585001), ComponentConfig{}
            .with_label(repo.fullFileError).with_size(ComponentSize{percent(1.f), pixels(100)})
            .with_font_size(pixels(14)).with_text_overflow(afterhours::ui::TextOverflow::Wrap)
            .with_debug_name("full_file_error"));
    } else if (!repo.fullFileDiff.empty() && repo.fullFileDiff.front().isBinary) {
        if (repo.fullFileHexPreviewKey != repo.fullFileCacheKey) {
            repo.fullFileHexPreviewKey = repo.fullFileCacheKey;
            repo.fullFileHexPreview = hex_view::make(repo.fullFileBytes, 0, 4096, repo.fullFilePage.begin.offset);
        }
        const auto& preview = repo.fullFileHexPreview;
        float bodyHeight = layout.mainContent.height - headerHeight - bookmarkHeight - blameHeight - pageHeight;
        constexpr float summaryHeight = 24.f;
        auto body = div(ctx, mk(parent, 585004), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(bodyHeight)})
            .with_flex_direction(FlexDirection::Column)
            .with_no_wrap().with_overflow(Overflow::Scroll, Axis::Y)
            .with_custom_background(theme::PANEL_BG)
            .with_padding(Padding{.top = pixels(8), .right = pixels(8), .bottom = pixels(8), .left = pixels(8)})
            .with_debug_name("hex_preview"));
        div(ctx, mk(body.ent(), 0), ComponentConfig{}
            .with_label(hex_view::summary(preview))
            .with_size(ComponentSize{percent(1.f), pixels(summaryHeight)})
            .with_font_size(pixels(12))
            .with_custom_text_color(theme::TEXT_SECONDARY)
            .with_debug_name("hex_preview_summary"));
        float offset = body.ent().get<afterhours::ui::HasScrollView>().scroll_offset.y / ui::zoom::get();
        float fontSize = Settings::get().get_code_font_size();
        float rowHeight = std::max(14.f, fontSize) * 1.4f;
        auto [first, last] = hex_view::visible_rows(preview.lines.size(), std::max(0.f, offset - summaryHeight), bodyHeight, rowHeight);
        auto spacer = [&](int id, float height) {
            if (height > 0.f) div(ctx, mk(body.ent(), id), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(height)}).with_skip_grid_snap());
        };
        spacer(1, static_cast<float>(first) * rowHeight);
        for (size_t row = first; row < last; ++row) {
            div(ctx, mk(body.ent(), 100 + static_cast<int>(row)), ComponentConfig{}
                .with_label(preview.lines[row])
                .with_size(ComponentSize{percent(1.f), pixels(rowHeight)})
                .with_skip_grid_snap()
                .with_font("mono", pixels(fontSize))
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_debug_name("hex_preview_line"));
        }
        spacer(2, static_cast<float>(preview.lines.size() - last) * rowHeight + 16.f);
    } else if (markdown && repo.fullFileMarkdownPreview) {
        auto& cache = repo.fullFileMarkdownCache;
        auto& fonts = EntityHelper::get_singleton_cmp_enforce<afterhours::ui::FontManager>();
        const auto bodyFont = fonts.get_font(afterhours::ui::UIComponent::DEFAULT_FONT);
        const auto codeFont = fonts.get_font("mono");
        const float zoom = ui::zoom::get();
        markdown_preview::update(cache, repo.fullFileCacheKey, repo.fullFileDecodedText,
            std::max(1.f, layout.mainContent.width - 48.f), 1.f,
            Settings::get().get_code_font_size(), 1.f,
            [&](const std::string& text, markdown_preview::Kind kind, float size) {
                return afterhours::measure_text(kind == markdown_preview::Kind::Code ? codeFont : bodyFont, text.c_str(), size * zoom, zoom).x / zoom;
            });
        float bodyHeight = layout.mainContent.height - headerHeight - bookmarkHeight - blameHeight - pageHeight;
        auto body = div(ctx, mk(parent, 585005), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(bodyHeight)})
            .with_flex_direction(FlexDirection::Column)
            .with_no_wrap().with_overflow(Overflow::Scroll, Axis::Y)
            .with_custom_background(theme::PANEL_BG)
            .with_padding(Padding{.top = pixels(8), .right = pixels(16), .bottom = pixels(8), .left = pixels(16)})
            .with_debug_name("markdown_preview"));
        ui::bind_reading_view(repo, body.ent(), "markdown:" + repo.fullFileCacheKey);
        auto& state = repo.reading;
        state.codeRows = false;
        state.key += ":" + std::to_string(cache.width) + ":" + std::to_string(cache.codeFontSize) + ":" + std::to_string(zoom);
        auto& scroll = body.ent().get<afterhours::ui::HasScrollView>();
        const auto wheel = afterhours::input::get_mouse_wheel_move_v();
        const bool scrolling = scroll.dragging_scrollbar || ((wheel.x != 0.f || wheel.y != 0.f) &&
            afterhours::ui::is_mouse_inside(ctx.mouse.pos, ui::visible_rect(body.ent())));
        if (scrolling) navigation::cancel_anchor(repo);
        else if (state.previousDocument == repo.workspace().active_id() && state.key != state.previousKey)
            navigation::restore_anchor(repo);
        const auto* document = repo.workspace().document(repo.workspace().active_id());
        if (document->restoreAnchor && document->anchor) {
            const auto& anchor = *document->anchor;
            size_t target = 0;
            for (size_t row = 0; row < cache.lines.size(); ++row) {
                const auto& line = cache.lines[row];
                if (line.sourceLine + repo.fullFilePage.begin.line - 1 > anchor.line) break;
                if (line.sourceLine + repo.fullFilePage.begin.line - 1 < anchor.line || line.sourceColumn <= anchor.column) target = row;
            }
            const float y = std::max(0.f, (cache.offsets[target] + 8.f - anchor.viewportFraction * bodyHeight) * zoom);
            scroll.scroll_offset.y = scroll.scroll_target.y = scroll.last_eased_offset.y = y;
            scroll.anchor_child = -1;
        }
        float offset = scroll.scroll_offset.y / zoom;
        auto [first, last] = markdown_preview::visible_rows(cache, offset, bodyHeight);
        auto spacer = [&](int id, float height) {
            if (height > 0.f) div(ctx, mk(body.ent(), id), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(height)}).with_skip_grid_snap());
        };
        spacer(0, cache.offsets[first]);
        for (size_t row = first; row < last; ++row) {
            const auto& line = cache.lines[row];
            auto renderedLine = div(ctx, mk(body.ent(), 100 + static_cast<int>(row)), ComponentConfig{}
                .with_label(line.text)
                .with_size(ComponentSize{percent(1.f), pixels(line.height)}).with_skip_grid_snap()
                .with_font(line.kind == markdown_preview::Kind::Code ? "mono" : afterhours::ui::UIComponent::DEFAULT_FONT, pixels(line.fontSize))
                .with_custom_text_color(line.kind == markdown_preview::Kind::Image ? theme::TEXT_SECONDARY : theme::TEXT_PRIMARY)
                .with_debug_name("markdown_preview_block"));
            const int endColumn = row + 1 < cache.lines.size() && cache.lines[row + 1].sourceLine == line.sourceLine
                ? cache.lines[row + 1].sourceColumn - 1 : std::numeric_limits<int>::max();
            const int sourceLine = line.sourceLine + repo.fullFilePage.begin.line - 1;
            state.rows.push_back({renderedLine.ent().id, repo.fullFilePath(), 0, 0, sourceLine, sourceLine, line.sourceColumn, endColumn});
        }
        spacer(1, cache.offsets.back() - cache.offsets[last] + 16.f);
    } else {
        ui::render_diff(ctx, parent, repo.fullFileDiff, layout.mainContent.width,
                        layout.mainContent.height - headerHeight - bookmarkHeight - blameHeight - pageHeight, false, changed, false,
                        repo.repoPath, nullptr, "file:" + repo.fullFileRevision());
    }
}

}
