#pragma once

#include <algorithm>
#include "../settings.h"
#include "diff_renderer.h"
#include "source_edit.h"
#include "source_header.h"
#include "../git/content_reader.h"
#include "../util/hex_view.h"
#include "../util/markdown_preview.h"
#include "../util/text_decode.h"
#include "../util/source_pages.h"

namespace ecs {

// Symbols of the file being read, from the cached outline scan over the
// reconstructed full-file text (bounded to 5000 lines).
inline const std::vector<symbol_outline::Symbol>& full_file_symbols(RepoComponent& repo) {
    static std::string key, text;
    static std::vector<symbol_outline::Symbol> symbols;
    size_t totalLines = 0;
    for (const auto& file : repo.fullFileDiff) for (const auto& hunk : file.hunks) totalLines += hunk.lines.size();
    const auto nextKey = repo.fullFilePath() + "\n" + std::to_string(repo.dataGeneration) + ":" + std::to_string(totalLines);
    if (nextKey != key) {
        key = nextKey;
        text.clear();
        symbols.clear();
        if (totalLines <= 5000) {
            for (const auto& file : repo.fullFileDiff)
                for (const auto& hunk : file.hunks)
                    for (const auto& line : hunk.lines) { text += line.substr(1); text += '\n'; }
            symbols = symbol_outline::cached_scan(repo.fullFilePath(), text);
        }
    }
    return symbols;
}

inline void open_markdown_link(RepoComponent& repo, int line) {
    auto target = repo.markdownLink.target;
    if (line <= 0) return navigation::open(repo, target, {}, reading::OpenMode::Keep);
    target.line = line;
    const auto anchor = reading::ReadingAnchor{target.destination.path, reading::anchor_revision(target), reading::DiffSide::After, line, 1, .15f};
    navigation::open(repo, target, {}, reading::OpenMode::Keep, anchor);
}

// Follows a relative link from the Markdown preview at the revision being
// read. "#L12" is a line; other anchors are matched against headings.
inline void follow_markdown_link(RepoComponent& repo, const std::string& link) {
    const auto hash = link.find('#');
    const auto path = markdown_preview::resolve(repo.fullFilePath(), std::string_view(link).substr(0, hash));
    if (path.empty()) return;
    auto& runtime = repo.markdownLink;
    runtime = {};
    runtime.target = reading::SourceLocation{{path, reading::source_revision_for(repo.workspace().location())}};
    runtime.anchor = hash == std::string::npos ? std::string{} : link.substr(hash + 1);
    const auto digits = runtime.anchor.size() > 1 && runtime.anchor[0] == 'L' &&
        std::all_of(runtime.anchor.begin() + 1, runtime.anchor.end(), [](unsigned char c) { return std::isdigit(c); });
    if (runtime.anchor.empty() || digits) return open_markdown_link(repo, digits ? std::atoi(runtime.anchor.c_str() + 1) : 0);
    runtime.request = navigation::stamp(repo, path + "#" + runtime.anchor);
    runtime.future = git::outline_source_async({repo.repoPath, path, repo.fullFileRevision(), {}, repo.fullFileEncodingOverride});
}

inline void poll_markdown_link(RepoComponent& repo) {
    auto& runtime = repo.markdownLink;
    if (!runtime.future.valid() || runtime.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const auto result = runtime.future.get();
    if (!navigation::accepts(repo, runtime.request, runtime.request.key)) return;
    int line = 0;
    for (const auto& symbol : result.symbols)
        if (symbol.kind == "heading" && markdown_preview::slug(symbol.name.substr(symbol.name.find(' ') + 1)) == runtime.anchor) {
            line = symbol.line;
            break;
        }
    open_markdown_link(repo, line);  // a missing heading opens the file at the top
}

inline void render_full_file(UIContext<InputAction>& ctx, Entity& parent,
                             RepoComponent& repo, LayoutComponent& layout) {
    // Edit mode: the document's buffer is authoritative, so the paged
    // read pipeline below is bypassed entirely and the same code renderer
    // draws the buffer instead.
    if (auto* editDoc = navigation::active_edit_document(repo);
        editDoc && editDoc->editMode && source_edit_eligible(repo)) {
        if (!editDoc->editSeeded && !seed_edit_buffer(repo, *editDoc)) editDoc->editMode = false;
        if (editDoc->editMode) {
            if (render_source_header(ctx, parent, repo, layout)) return;
            bool rebuilt = false;
            if (!repo.editRendered || repo.editRenderedGeneration != editDoc->editGeneration) {
                repo.editDiff.clear();
                repo.editDiff.push_back(git::parse_complete_file(repo.fullFilePath(), edit_buffer_text(*editDoc)));
                repo.editRenderedGeneration = editDoc->editGeneration;
                repo.editRendered = true;
                rebuilt = true;
            }
            ui::render_diff(ctx, parent, repo.editDiff, layout.mainContent.width,
                            layout.mainContent.height - 32.f, false, rebuilt, false,
                            repo.repoPath, nullptr, "file:edit");
            apply_edit_input(ctx, repo, layout);
            return;
        }
    }
    if (repo.fullFileNavigateFrames > 0) repo.fullFileMarkdownPreview = false;
    std::string sourceKey = repo.repoPath + "\n" + repo.fullFileRevision() + "\n" + repo.fullFilePath();
    if (repo.fullFileRevision().empty() || repo.fullFileRevision() == "INDEX") sourceKey += ":" + std::to_string(repo.dataGeneration);
    if (repo.fullFileSourceKey != sourceKey) {
        repo.fullFileSourceKey = sourceKey;
        repo.fullFilePage = {};
        repo.sourceWindow = {};
        repo.fullFilePageRequest = {};
        repo.fullFileRequestedTargetLine = repo.fullFileRequestedTargetColumn = 0;
    }
    const int targetColumn = repo.workspace().active_source()->column;
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
        repo.fullFileExtendRequest = !repo.fullFileDiff.empty() && !repo.fullFileDiff.front().isBinary &&
            !repo.fullFileMarkdownPreview && !repo.sourceWindow.pages.empty() &&
            ((pageRequest.action == FilePageRequest::Action::Next && pageRequest.cursor.offset == repo.fullFilePage.next.offset) ||
             (pageRequest.action == FilePageRequest::Action::Previous && pageRequest.cursor.offset == repo.fullFilePage.begin.offset));
        if (!repo.fullFileExtendRequest) {
            repo.fullFileDiff.clear();
            repo.sourceFoldRanges.clear();
            repo.sourceFoldIdentity = 0;
            repo.sourceWindow = {};
            repo.fullFileDecodedText.clear();
        }
        repo.fullFileError.clear();
        repo.blameOpen = false;
        repo.blameFuture = {};
        repo.fullFileRequestStamp = navigation::stamp(repo, key);
        repo.fullFileLoading.restart();
        const git::FileRequest request{repo.repoPath, repo.fullFilePath(), repo.fullFileRevision(),
            pageRequest, repo.fullFileEncodingOverride, repo.fullFilePage.encoding};
        repo.fullFileRevalidate = {};
        if (auto cached = git::peek_file(request)) {
            repo.fullFileFuture = async_work::completed(std::move(*cached));
            repo.fullFileRevalidate = git::read_file_async(request);
        } else repo.fullFileFuture = git::read_file_async(request);
        if (repo.fullFileExtendRequest) changed = false;
    }
    bool pageLayoutReady = true;
    if (repo.fullFileExtendRequest && !repo.pendingCaret) {
        const auto* document = repo.workspace().document(repo.workspace().active_id());
        auto viewport = afterhours::ui::UICollectionHolder::getEntityForID(repo.reading.entity);
        pageLayoutReady = document->anchor && !document->restoreAnchor && viewport.valid() &&
            (**viewport).has<afterhours::ui::HasScrollView>();
        if (pageLayoutReady) {
            const auto& scroll = (**viewport).get<afterhours::ui::HasScrollView>();
            const auto wheel = afterhours::input::get_mouse_wheel_move_v();
            const bool scrolling = scroll.dragging_scrollbar || ((wheel.x != 0.f || wheel.y != 0.f) &&
                afterhours::ui::is_mouse_inside(ctx.mouse.pos, ui::visible_rect(**viewport)));
            pageLayoutReady = !scrolling && repo.reading.previousDocument == document->id &&
                std::fabs(repo.reading.offset - scroll.scroll_offset.y) < .5f;
        }
    }
    if (!repo.fullFileFuture.valid() && repo.fullFileRevalidate.valid() &&
        repo.fullFileRevalidate.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto checked = repo.fullFileRevalidate.get();
        if (navigation::accepts(repo, repo.fullFileRequestStamp, key) &&
            (!checked.error.empty() || checked.page.sourceIdentity != repo.fullFilePage.sourceIdentity))
            repo.fullFileFuture = async_work::completed(std::move(checked));
    }
    if (pageLayoutReady && repo.fullFileFuture.valid() && repo.fullFileFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto content = repo.fullFileFuture.get();
        if (!navigation::accepts(repo, repo.fullFileRequestStamp, key)) {
            repo.fullFileCacheKey.clear();
            return;
        }
        reading_load::Publishing publication(repo.fullFileTrace, content.trace);
        if (navigation::resolve_source(repo, repo.fullFileRequestStamp, content.resolvedRevision)) {
            auto resolvedKey = repo.repoPath + "\n" + repo.fullFileRevision() + "\n" + repo.fullFilePath();
            repo.fullFileCacheKey.replace(0, sourceKey.size(), resolvedKey);
            repo.fullFileSourceKey = resolvedKey;
        }
        repo.fullFileError = std::move(content.error);
        if (repo.fullFileError.empty()) {
            if (repo.fullFileExtendRequest) {
                if (source_pages::extend(repo.sourceWindow, std::move(content.page), std::move(content.raw))) {
                    repo.fullFilePage = source_pages::range(repo.sourceWindow);
                    auto decoded = file_page::decode(repo.sourceWindow.raw, repo.fullFilePage.encoding, repo.fullFilePage.begin.offset);
                    auto file = git::parse_complete_file(repo.fullFilePath(), decoded.text, repo.fullFilePage.begin.lexical);
                    file.oldMode = file.newMode = content.diff.newMode;
                    file.isPartialContent = repo.fullFilePage.begin.offset != 0 || repo.fullFilePage.next.offset < repo.fullFilePage.totalBytes;
                    if (!file.hunks.empty()) file.hunks.front().oldStart = file.hunks.front().newStart = repo.fullFilePage.begin.line;
                    repo.fullFileDiff.clear();
                    repo.fullFileDiff.push_back(std::move(file));
                    repo.fullFileEncodingLabel = std::move(decoded.encoding);
                    if (markdown_preview::is_markdown_path(repo.fullFilePath())) repo.fullFileDecodedText = std::move(decoded.text);
                    navigation::restore_anchor(repo);
                } else repo.fullFileError = "Source changed while scrolling. Reload file to continue.";
            } else {
                repo.sourceWindow = {{content.page}, std::move(content.raw)};
                repo.fullFilePage = std::move(content.page);
                repo.fullFileEncodingLabel = std::move(content.encodingLabel);
                repo.fullFileDecodedText = std::move(content.decodedText);
                repo.fullFileDiff.clear();
                repo.fullFileDiff.push_back(std::move(content.diff));
            }
        }
        if (!repo.fullFileDiff.empty() && !repo.fullFileDiff.front().isBinary) {
            repo.fullFileHexPreviewKey.clear();
            repo.fullFileHexPreview = {};
        }
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
    if (!repo.fullFileDiff.empty() && !repo.fullFileDiff.front().isBinary) {
        const auto& file = repo.fullFileDiff.front();
        navigation::sync_source_folds(repo, repo.fullFilePage.sourceIdentity + "\n" + repo.fullFilePage.encoding);
        if (repo.sourceFoldIdentity != file.renderIdentity) {
            repo.sourceFoldIdentity = file.renderIdentity;
            auto lines = ui::diff_sel::code_lines(file, reading::DiffSide::After, repo.fullFilePage.begin.column);
            repo.sourceFoldRanges = source_folding::discover(repo.fullFilePath(), lines, repo.fullFilePage.begin.lexical,
                repo.fullFilePage.next.offset == repo.fullFilePage.totalBytes);
        }
    }
    constexpr float headerHeight = 32.f;
    if (render_source_header(ctx, parent, repo, layout)) return;
    const auto& page = repo.fullFilePage;
    bool partial = page.begin.offset != 0 || page.next.offset < page.totalBytes;
    const bool manualPages = repo.fullFileMarkdownPreview || !repo.fullFileHexPreviewKey.empty() ||
        (!repo.fullFileDiff.empty() && repo.fullFileDiff.front().isBinary);
    float pageHeight = manualPages && (partial || repo.fullFileFuture.valid() || !repo.fullFileError.empty()) ? 64.f : 0.f;
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
    if (repo.fullFileFuture.valid() && repo.fullFileDiff.empty()) {
        if (repo.fullFileLoading.visible(true)) div(ctx, mk(parent, 585003), ComponentConfig{}
            .with_label("Loading file...").with_size(ComponentSize{percent(1.f), pixels(40)})
            .with_font_size(pixels(14)).with_debug_name("full_file_loading"));
    } else if (!repo.fullFileError.empty() && repo.fullFileDiff.empty()) {
        div(ctx, mk(parent, 585001), ComponentConfig{}
            .with_label(repo.fullFileError).with_size(ComponentSize{percent(1.f), pixels(100)})
            .with_font_size(pixels(14)).with_text_overflow(afterhours::ui::TextOverflow::Wrap)
            .with_debug_name("full_file_error"));
    } else if (!repo.fullFileDiff.empty() && repo.fullFileDiff.front().isBinary) {
        if (repo.fullFileHexPreviewKey != repo.fullFileCacheKey) {
            repo.fullFileHexPreviewKey = repo.fullFileCacheKey;
            repo.fullFileHexPreview = hex_view::make(repo.sourceWindow.raw, 0, 4096, repo.fullFilePage.begin.offset);
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
        poll_markdown_link(repo);
        auto& cache = repo.fullFileMarkdownCache;
        auto& fonts = EntityHelper::get_singleton_cmp_enforce<afterhours::ui::FontManager>();
        const auto bodyFont = fonts.get_font(afterhours::ui::UIComponent::DEFAULT_FONT);
        const auto codeFont = fonts.get_font("mono");
        const float zoom = ui::zoom::get();
        markdown_preview::update(cache, repo.fullFileCacheKey, repo.fullFileDecodedText,
            std::max(1.f, layout.mainContent.width - 48.f), 1.f,
            Settings::get().get_code_font_size(), 1.f,
            [&](const std::string& text, markdown_preview::Kind kind, float size) {
                return afterhours::measure_text(markdown_preview::literal(kind) ? codeFont : bodyFont, text.c_str(), size * zoom, zoom).x / zoom;
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
        const auto* menu = find_singleton<MenuComponent>();
        const bool overlayOpen = ui::is_context_menu_open() || (menu && menu->activeMenuIndex >= 0);
        std::optional<std::string> followed;
        for (size_t row = first; row < last; ++row) {
            const auto& line = cache.lines[row];
            const auto color = line.kind == markdown_preview::Kind::Image ? theme::TEXT_SECONDARY : theme::TEXT_PRIMARY;
            std::vector<afterhours::ui::TextSpan> spans;
            size_t at = 0;
            // Bold and italic draw plain: no italic face ships, and weighted
            // runs draw small (docs/afterhours-gaps.md).
            for (const auto& span : line.spans) {
                using markdown_preview::Style;
                if (span.begin > at) spans.push_back({line.text.substr(at, span.begin - at), color});
                spans.push_back({line.text.substr(span.begin, span.end - span.begin),
                    span.style == Style::Link ? theme::TEXT_ACCENT : span.style == Style::Code ? theme::STATUS_MODIFIED : color});
                at = span.end;
            }
            if (!spans.empty() && at < line.text.size()) spans.push_back({line.text.substr(at), color});
            auto config = ComponentConfig{}
                .with_label(line.text)
                .with_size(ComponentSize{percent(1.f), pixels(line.height)}).with_skip_grid_snap()
                .with_font(markdown_preview::literal(line.kind) ? "mono" : afterhours::ui::UIComponent::DEFAULT_FONT, pixels(line.fontSize))
                .with_custom_text_color(color)
                .with_debug_name("markdown_preview_block");
            if (!spans.empty()) config.with_styled_label(std::move(spans));
            auto renderedLine = div(ctx, mk(body.ent(), 100 + static_cast<int>(row)), config);
            const int endColumn = row + 1 < cache.lines.size() && cache.lines[row + 1].sourceLine == line.sourceLine
                ? cache.lines[row + 1].sourceColumn - 1 : std::numeric_limits<int>::max();
            const int sourceLine = line.sourceLine + repo.fullFilePage.begin.line - 1;
            state.rows.push_back({renderedLine.ent().id, repo.fullFilePath(), 0, 0, sourceLine, sourceLine, line.sourceColumn, endColumn});
            if (line.spans.empty() || overlayOpen || !ctx.mouse.just_released) continue;
            const auto rect = ui::visible_rect(renderedLine.ent());
            if (!afterhours::ui::is_mouse_inside(ctx.mouse.pos, rect)) continue;
            const auto width = [&](size_t bytes) {
                return afterhours::measure_text(bodyFont, line.text.substr(0, bytes).c_str(), line.fontSize * zoom, zoom).x;
            };
            for (const auto& span : line.spans)
                if (const float x = ctx.mouse.pos.x - rect.x;
                    span.style == markdown_preview::Style::Link && x >= width(span.begin) && x < width(span.end)) followed = span.target;
        }
        spacer(1, cache.offsets.back() - cache.offsets[last] + 16.f);
        if (followed) follow_markdown_link(repo, *followed);  // navigating resets `cache`
    } else {
        float bodyHeight2 = layout.mainContent.height - headerHeight - blameHeight - pageHeight;
        // Sticky scope: pin the enclosing symbol whose declaration has
        // scrolled above the viewport top (projectedLine, kept by the
        // diff renderer). Rendered in flow so it never hides under chrome.
        const symbol_outline::Symbol* stickyScope = nullptr;
        if (layout.stickyScopes && !repo.fullFileDiff.empty()) {
            const int firstVisible = repo.reading.projectedLine.value_or(0);
            if (firstVisible > 0)
                for (const auto& symbol : full_file_symbols(repo))
                    if (symbol.line < firstVisible) stickyScope = &symbol;
        }
        if (stickyScope) bodyHeight2 = std::max(24.f, bodyHeight2 - 24.f);
        // Blame gutter: load whole-file blame on a worker (keyed by path,
        // revision and data generation, so an external edit re-blames) and
        // lend it to the gutter for this render only.
        struct BlameGutterGuard {
            ~BlameGutterGuard() { code_gutter::blameFor = nullptr; code_gutter::blameChars = 0; }
        } blameGutterGuard;
        if (layout.blameGutter && !repo.fullFileDiff.empty() && repo.fullFileDiff.front().isFullContent &&
            !repo.fullFilePath().empty()) {
            const std::string key = repo.repoPath + "\n" + repo.fullFilePath() + "\n" +
                repo.fullFileRevision() + "\n" + std::to_string(repo.dataGeneration);
            if (repo.blameGutterFuture.valid() &&
                repo.blameGutterFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                auto result = repo.blameGutterFuture.get();
                repo.blameGutterFuture = {};
                if (navigation::accepts(repo, repo.blameGutterStamp, repo.blameGutterStamp.key)) {
                    repo.blameGutterError = result.success() ? "" : result.stderr_str();
                    repo.blameGutterLines = result.success() ? git::parse_blame_porcelain(result.stdout_str())
                                                             : std::vector<BlameLine>{};
                }
            }
            if (repo.blameGutterKey != key && !repo.blameGutterFuture.valid()) {
                repo.blameGutterKey = key;
                repo.blameGutterLines.clear();
                repo.blameGutterError.clear();
                std::vector<std::string> args{"blame", "--porcelain"};
                if (!repo.fullFileRevision().empty() && repo.fullFileRevision() != "INDEX")
                    args.push_back(repo.fullFileRevision());
                args.insert(args.end(), {"--", repo.fullFilePath()});
                repo.blameGutterStamp = navigation::stamp(repo, key);
                repo.blameGutterFuture = git::git_run_async(repo.repoPath, args);
            }
            if (repo.blameGutterKey == key && !repo.blameGutterLines.empty()) {
                const auto& lines = repo.blameGutterLines;
                size_t authorWidth = 0;
                for (const auto& line : lines) authorWidth = std::max(authorWidth, std::min(line.author.size(), size_t{14}));
                code_gutter::blameChars = static_cast<int>(authorWidth) + 11;
                code_gutter::blameFor = [&lines, authorWidth](int number) {
                    std::string out;
                    if (number >= 1 && static_cast<size_t>(number) <= lines.size() &&
                        lines[static_cast<size_t>(number) - 1].finalLine == number) {
                        const auto& line = lines[static_cast<size_t>(number) - 1];
                        out = line.hash == std::string(40, '0') ? std::string("Not committed").substr(0, authorWidth)
                                                                : line.author.substr(0, authorWidth);
                        if (out.size() < authorWidth) out += std::string(authorWidth - out.size(), ' ');
                        if (!line.date.empty()) out += " " + line.date;
                    }
                    if (static_cast<int>(out.size()) < code_gutter::blameChars)
                        out += std::string(static_cast<size_t>(code_gutter::blameChars) - out.size(), ' ');
                    return out.substr(0, static_cast<size_t>(code_gutter::blameChars));
                };
            }
        }
        if (layout.outlinePanelOpen && !repo.fullFileDiff.empty()) {
            // Persistent outline: symbols of the current file, following
            // the caret; single click scrolls (preview), double click jumps.
            const auto& symbols = full_file_symbols(repo);
            const auto* document = repo.workspace().document(repo.workspace().active_id());
            const int cursorLine = document && document->caret ? document->caret->line :
                document && document->anchor ? document->anchor->line : 0;
            int enclosing = -1;
            for (size_t i = 0; i < symbols.size(); ++i) if (symbols[i].line <= cursorLine) enclosing = static_cast<int>(i);
            auto bodyRow = div(ctx, mk(parent, 585030), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(bodyHeight2)}).with_flex_direction(FlexDirection::Row)
                .with_no_wrap().with_debug_name("outline_body"));
            auto panel = div(ctx, mk(bodyRow.ent(), 0), ComponentConfig{}
                .with_size(ComponentSize{pixels(220), pixels(bodyHeight2)}).with_flex_direction(FlexDirection::Column)
                .with_custom_background(theme::PANEL_BG).with_debug_name("outline_panel"));
            div(ctx, mk(panel.ent(), 0), ComponentConfig{}.with_label("Outline")
                .with_size(ComponentSize{percent(1.f), pixels(26)}).with_font_size(pixels(12))
                .with_custom_text_color(theme::TEXT_SECONDARY).with_debug_name("outline_title"));
            auto& mutableLayout = const_cast<LayoutComponent&>(layout);
            ui::virtual_list(ctx, mk(panel.ent(), 1), symbols.size(), 24.f, [&](size_t i, Entity& row) {
                const auto& symbol = symbols[i];
                auto label = button(ctx, mk(row, 0), preset::Button(symbol.name)
                    .with_size(ComponentSize{percent(1.f), pixels(24)}).with_alignment(TextAlignment::Left)
                    .with_custom_background(static_cast<int>(i) == enclosing ? theme::BUTTON_PRIMARY : theme::PANEL_BG)
                    .with_font_size(pixels(12)).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_debug_name("outline_symbol_" + std::to_string(i)));
                ui::set_tooltip(label.ent(), symbol.kind + " · line " + std::to_string(symbol.line));
                if (!label) return;
                const auto now = std::chrono::steady_clock::now();
                const bool again = mutableLayout.outlinePanelClickLine == symbol.line &&
                    now - mutableLayout.outlinePanelClickAt <= std::chrono::milliseconds(500);
                mutableLayout.outlinePanelClickLine = symbol.line;
                mutableLayout.outlinePanelClickAt = now;
                auto anchor = reading::ReadingAnchor{repo.fullFilePath(), repo.fullFileRevision(),
                    reading::DiffSide::After, symbol.line, 1, .15f, ' '};
                auto target = reading::SourceLocation{{repo.fullFilePath(),
                    reading::source_revision_for(repo.workspace().location())}, symbol.line};
                navigation::open(repo, std::move(target), {}, again ? reading::OpenMode::Keep : reading::OpenMode::Preview, anchor);
            }, ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(std::max(24.f, bodyHeight2 - 26.f))})
                .with_debug_name("outline_list"));
            auto content = div(ctx, mk(bodyRow.ent(), 1), ComponentConfig{}
                .with_size(ComponentSize{expand(), pixels(bodyHeight2)}).with_debug_name("outline_content"));
            ui::render_diff(ctx, content.ent(), repo.fullFileDiff, std::max(1.f, layout.mainContent.width - 224.f),
                            bodyHeight2, false, changed, false,
                            repo.repoPath, nullptr, "file:" + repo.fullFileRevision());
        } else if (stickyScope) {
            // The diff positions absolutely inside its parent, so render
            // it into a container that starts below the sticky bar.
            auto holder = div(ctx, mk(parent, 585041), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(bodyHeight2 + 24.f)})
                .with_flex_direction(FlexDirection::Column).with_debug_name("sticky_body"));
            auto sticky = div(ctx, mk(holder.ent(), 0), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(24)}).with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center).with_custom_background(theme::PANEL_BG)
                .with_debug_name("sticky_scope"));
            auto jump = button(ctx, mk(sticky.ent(), 0), preset::Button(stickyScope->name + " · line " + std::to_string(stickyScope->line))
                .with_size(ComponentSize{children(), pixels(22)}).with_font_size(pixels(12))
                .with_custom_background(theme::SELECTED_BG).with_custom_text_color(theme::TEXT_PRIMARY)
                .with_debug_name("sticky_scope_jump"));
            ui::set_tooltip(jump.ent(), "Jump to " + stickyScope->name);
            if (jump) {
                auto anchor = reading::ReadingAnchor{repo.fullFilePath(), repo.fullFileRevision(),
                    reading::DiffSide::After, stickyScope->line, 1, .15f, ' '};
                auto target = reading::SourceLocation{{repo.fullFilePath(),
                    reading::source_revision_for(repo.workspace().location())}, stickyScope->line};
                navigation::open(repo, std::move(target), {}, reading::OpenMode::Keep, anchor);
            }
            auto content = div(ctx, mk(holder.ent(), 1), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(bodyHeight2)}).with_debug_name("sticky_content"));
            ui::render_diff(ctx, content.ent(), repo.fullFileDiff, layout.mainContent.width,
                            bodyHeight2, false, changed, false,
                            repo.repoPath, nullptr, "file:" + repo.fullFileRevision());
        } else if (layout.minimap && !repo.fullFileDiff.empty()) {
            // Minimap (off by default): bucketed line-density strips with a
            // viewport marker; clicking a strip jumps to its first line.
            auto bodyRow = div(ctx, mk(parent, 585050), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(bodyHeight2)}).with_flex_direction(FlexDirection::Row)
                .with_no_wrap().with_debug_name("minimap_body"));
            auto content = div(ctx, mk(bodyRow.ent(), 0), ComponentConfig{}
                .with_size(ComponentSize{expand(), pixels(bodyHeight2)}).with_debug_name("minimap_content"));
            ui::render_diff(ctx, content.ent(), repo.fullFileDiff, std::max(1.f, layout.mainContent.width - 84.f),
                            bodyHeight2, false, changed, false,
                            repo.repoPath, nullptr, "file:" + repo.fullFileRevision());
            const auto& file = repo.fullFileDiff.front();
            std::vector<int> lineLengths(1, 0);
            for (const auto& hunk : file.hunks) {
                int lineNo = hunk.newStart;
                for (const auto& line : hunk.lines) {
                    if (!line.empty() && line.front() == '-') continue;
                    if (static_cast<size_t>(lineNo) >= lineLengths.size()) lineLengths.resize(static_cast<size_t>(lineNo) + 1, 0);
                    size_t length = line.size() > 1 ? line.size() - 1 : 0;
                    while (length > 0 && (line[length] == ' ' || line[length] == '\t')) --length;
                    lineLengths[static_cast<size_t>(lineNo)] = static_cast<int>(length);
                    ++lineNo;
                }
            }
            const int total = static_cast<int>(lineLengths.size()) - 1;
            auto minimapCol = div(ctx, mk(bodyRow.ent(), 1), ComponentConfig{}
                .with_size(ComponentSize{pixels(72), pixels(bodyHeight2)}).with_flex_direction(FlexDirection::Column)
                .with_no_wrap().with_custom_background(theme::PANEL_BG).with_debug_name("minimap"));
            if (total > 0) {
                const int buckets = std::max(1, std::min(total, static_cast<int>(bodyHeight2 / 4.f)));
                const int perBucket = (total + buckets - 1) / buckets;
                const int firstVisible = repo.reading.projectedLine.value_or(1);
                const int visibleLines = std::max(1, static_cast<int>(bodyHeight2 / 20.f));
                const float bucketHeight = bodyHeight2 / static_cast<float>(buckets);
                for (int b = 0; b < buckets; ++b) {
                    const int begin = b * perBucket + 1;
                    const int end = std::min(total, begin + perBucket - 1);
                    int longest = 0;
                    for (int line = begin; line <= end; ++line) longest = std::max(longest, lineLengths[static_cast<size_t>(line)]);
                    const bool inView = end >= firstVisible && begin <= firstVisible + visibleLines;
                    const float stripWidth = 4.f + 60.f * std::min(longest, 72) / 72.f;
                    auto strip = button(ctx, mk(minimapCol.ent(), b), preset::Button("")
                        .with_size(ComponentSize{pixels(stripWidth), pixels(bucketHeight)})
                        .with_custom_background(inView ? theme::SELECTED_BG : longest > 0 ? theme::TEXT_TERTIARY : theme::PANEL_BG)
                        .with_debug_name("minimap_strip_" + std::to_string(b)));
                    if (strip) {
                        auto anchor = reading::ReadingAnchor{repo.fullFilePath(), repo.fullFileRevision(),
                            reading::DiffSide::After, begin, 1, .05f, ' '};
                        auto target = reading::SourceLocation{{repo.fullFilePath(),
                            reading::source_revision_for(repo.workspace().location())}, begin};
                        navigation::open(repo, std::move(target), {}, reading::OpenMode::Keep, anchor);
                    }
                }
            }
        } else
        ui::render_diff(ctx, parent, repo.fullFileDiff, layout.mainContent.width,
                        bodyHeight2, false, changed, false,
                        repo.repoPath, nullptr, "file:" + repo.fullFileRevision());
        if (!repo.fullFileError.empty()) {
            div(ctx, mk(parent, 585001), ComponentConfig{}.with_skip_grid_snap()
                .with_label(repo.fullFileError).with_size(ComponentSize{percent(1.f), pixels(32)})
                .with_absolute_position(0.f, headerHeight).with_font_size(pixels(12))
                .with_custom_background(theme::PANEL_BG).with_debug_name("full_file_error"));
        }
        // Hover/type info anchored to its token; dismissed by moving the
        // caret or pressing Escape.
        if (!repo.hoverInfo.empty()) {
            const auto* document = repo.workspace().document(repo.workspace().active_id());
            const bool moved = !document || !document->caret || document->caret->path != repo.hoverAnchor.path ||
                document->caret->line != repo.hoverAnchor.line || document->caret->column != repo.hoverAnchor.column;
            if (moved || afterhours::input::is_key_pressed(afterhours::keys::ESCAPE)) repo.hoverInfo.clear();
            else {
                for (const auto& row : ui::diff_sel::state().lastLines) {
                    if (row.filePath != repo.hoverAnchor.path || row.newLine != repo.hoverAnchor.line) continue;
                    const float x = std::max(0.f, std::min(row.contentX0, layout.mainContent.width - 200.f));
                    auto tip = div(ctx, mk(parent, 585050), ComponentConfig{}.with_skip_grid_snap()
                        .with_label(repo.hoverInfo).with_size(ComponentSize{pixels(440), children()})
                        .with_absolute_position(x, row.rect.y + row.rect.height + 2.f)
                        .with_custom_background(theme::PANEL_BG).with_border(theme::BORDER, pixels(1))
                        .with_corner_radius(6.f).with_render_layer(900)
                        .with_font_size(pixels(12)).with_text_overflow(afterhours::ui::TextOverflow::Wrap)
                        .with_padding(Padding{.top = pixels(6), .bottom = pixels(6), .left = pixels(8), .right = pixels(8)})
                        .with_debug_name("hover_info"));
                    (void)tip;
                    break;
                }
            }
        }
        if (!changed && !repo.fullFileFuture.valid() && repo.fullFileError.empty() && !repo.pendingCaret &&
            !repo.workspace().document(repo.workspace().active_id())->restoreAnchor) {
            auto viewport = afterhours::ui::UICollectionHolder::getEntityForID(repo.reading.entity);
            if (viewport.valid() && (**viewport).has<afterhours::ui::HasScrollView>()) {
                const auto& scroll = (**viewport).get<afterhours::ui::HasScrollView>();
                const float height = scroll.viewport_or_zero().y;
                if (height > 0.f) {
                    std::optional<FilePageRequest> next;
                    if (scroll.scroll_offset.y + height >= scroll.content_size.y - height * .75f && page.next.offset < page.totalBytes)
                        next = FilePageRequest{FilePageRequest::Action::Next, page.next, 0, page.sourceIdentity};
                    else if (scroll.scroll_offset.y < height * .75f && page.begin.offset > 0)
                        next = FilePageRequest{FilePageRequest::Action::Previous, page.begin, 0, page.sourceIdentity};
                    if (next) {
                        repo.fullFilePageRequest = std::move(*next);
                        repo.fullFileCacheKey.clear();
                    }
                }
            }
        }
    }
}

}
