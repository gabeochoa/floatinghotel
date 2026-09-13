#pragma once

#include <algorithm>
#include "../settings.h"
#include "diff_renderer.h"
#include "source_header.h"
#include "../git/content_reader.h"
#include "../util/hex_view.h"
#include "../util/markdown_preview.h"
#include "../util/text_decode.h"

namespace ecs {

inline void render_full_file(UIContext<InputAction>& ctx, Entity& parent,
                             RepoComponent& repo, LayoutComponent& layout) {
    if (repo.fullFileNavigateFrames > 0) repo.fullFileMarkdownPreview = false;
    std::string sourceKey = repo.repoPath + "\n" + repo.fullFileRevision() + "\n" + repo.fullFilePath();
    if (repo.fullFileRevision().empty() || repo.fullFileRevision() == "INDEX") sourceKey += ":" + std::to_string(repo.dataGeneration);
    if (repo.fullFileSourceKey != sourceKey) {
        repo.fullFileSourceKey = sourceKey;
        repo.fullFilePage = {};
        repo.fullFilePageRequest = {};
        repo.fullFileRequestedTargetLine = repo.fullFileRequestedTargetColumn = 0;
    }
    const int targetColumn = repo.workspace().source()->column;
    if (repo.fullFileTargetLine() > 0 && (repo.fullFileRequestedTargetLine != repo.fullFileTargetLine() || repo.fullFileRequestedTargetColumn != targetColumn)) {
        repo.fullFileRequestedTargetLine = repo.fullFileTargetLine();
        repo.fullFileRequestedTargetColumn = targetColumn;
        if (repo.fullFileDiff.empty() || !repo.fullFilePage.contains(repo.fullFileTargetLine(), targetColumn, 0)) {
            const auto* document = repo.workspace().document(repo.workspace().active_id());
            const int leading = document->restoreAnchor && document->anchor
                ? static_cast<int>(std::ceil(layout.mainContent.height * document->anchor->viewportFraction /
                    (Settings::get().get_code_font_size() + 8.f))) + 1 : 0;
            repo.fullFilePageRequest = {FilePageRequest::Action::TargetLine, {}, repo.fullFileTargetLine(), {}, leading, targetColumn};
        }
    }
    const auto& pageRequest = repo.fullFilePageRequest;
    std::string key = sourceKey + "\n" + repo.fullFileEncodingOverride + ":" +
        std::to_string(static_cast<int>(pageRequest.action)) + ":" + std::to_string(pageRequest.cursor.offset) + ":" + std::to_string(pageRequest.targetLine) + ":" + std::to_string(pageRequest.leadingLines) + ":" + std::to_string(pageRequest.targetColumn);
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
        if (repo.pendingCaret) {
            auto position = *repo.pendingCaret;
            const auto motion = repo.pendingCaretMotion;
            repo.pendingCaret.reset();
            repo.pendingCaretMotion.reset();
            if (repo.fullFileError.empty() && !repo.fullFileDiff.front().isBinary) {
                auto lines = ui::diff_sel::code_lines(repo.fullFileDiff.front(), reading::DiffSide::After, repo.fullFilePage.begin.column);
                if (!lines.empty()) {
                    if (motion) position = reading::move_code(position, *motion, lines);
                    else {
                        auto line = std::lower_bound(lines.begin(), lines.end(), position.line,
                            [](const reading::CodeLine& row, int number) { return row.number < number; });
                        if (line == lines.end()) line = std::prev(lines.end());
                        position.line = line->number;
                        position.column = std::clamp(position.column, line->column, reading::end_column(*line));
                    }
                    navigation::reveal_caret(repo, position, motion == reading::CodeMotion::DocumentEnd ? .85f : .15f);
                    if (auto selection = repo.workspace().document(repo.workspace().active_id())->selection) {
                        selection->head = position;
                        navigation::set_selection(repo, std::move(selection));
                    }
                    navigation::focus_document(repo, reading::focus::Region::Code);
                }
            }
        }
        changed = true;
    }
    constexpr float headerHeight = 32.f;
    if (render_source_header(ctx, parent, repo, layout)) return;
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
            repo.fullFileRequestedTargetLine = repo.fullFileRequestedTargetColumn = 0;
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
            (page.begin.continuation || (page.next.offset < page.totalBytes && page.next.continuation) ? " · line fragment" : "");
        div(ctx, mk(parent, 585021), ComponentConfig{}.with_label(range)
            .with_size(ComponentSize{percent(1.f), pixels(32)}).with_font_size(pixels(12))
            .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("file_page_range"));
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
        float bodyHeight = layout.mainContent.height - headerHeight - blameHeight - pageHeight;
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
    } else if (markdown_preview::is_markdown_path(repo.fullFilePath()) && repo.fullFileMarkdownPreview) {
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
        float bodyHeight = layout.mainContent.height - headerHeight - blameHeight - pageHeight;
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
                        layout.mainContent.height - headerHeight - blameHeight - pageHeight, false, changed, false,
                        repo.repoPath, nullptr, "file:" + repo.fullFileRevision());
    }
}

}
