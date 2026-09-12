#pragma once

#include <algorithm>
#include "../settings.h"
#include "diff_renderer.h"
#include "file_history.h"
#include "../git/content_reader.h"
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
    std::string key = repo.repoPath + "\n" + repo.fullFileRevision + "\n" + repo.fullFilePath;
    if (repo.fullFileRevision.empty() || repo.fullFileRevision == "INDEX") key += ":" + std::to_string(repo.dataGeneration);
    key += "\n" + repo.fullFileEncodingOverride;
    bool changed = repo.fullFileCacheKey != key;
    if (changed) {
        repo.fullFileCacheKey = key;
        repo.fullFileDiff.clear();
        repo.fullFileError.clear();
        repo.fullFileBytes.clear();
        repo.blameOpen = false;
        repo.blameFuture = {};
        repo.fullFileFuture = git::read_file_async({repo.repoPath, repo.fullFilePath, repo.fullFileRevision, repo.fullFileEncodingOverride});
    }
    if (repo.fullFileFuture.valid() && repo.fullFileFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto content = repo.fullFileFuture.get();
        repo.fullFileError = std::move(content.error);
        repo.fullFileBytes = std::move(content.raw);
        repo.fullFileEncodingLabel = std::move(content.encodingLabel);
        if (repo.fullFileError.empty()) repo.fullFileDiff.push_back(std::move(content.diff));
        changed = true;
    }
    auto header = div(ctx, mk(parent, 585000), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(34)})
        .with_flex_direction(FlexDirection::Row).with_debug_name("full_file_header"));
    if (button(ctx, mk(header.ent(), 0), preset::Button("Back to diff")
            .with_size(ComponentSize{pixels(110), pixels(30)}).with_debug_name("full_file_back"))) {
        repo.fullFilePath.clear();
        repo.fullFileFuture = {};
    }
    div(ctx, mk(header.ent(), 1), ComponentConfig{}
        .with_label(repo.fullFilePath + " @ " + (repo.fullFileRevision.empty() ? "working tree" : repo.fullFileRevision))
        .with_size(ComponentSize{expand(), pixels(30)}).with_font_size(FontSize::Small)
        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
        .with_debug_name("full_file_revision"));
    if (button(ctx, mk(header.ent(), 2), preset::Button("History")
            .with_size(ComponentSize{pixels(75), pixels(30)}).with_debug_name("file_history_open")))
        open_file_history(repo, repo.fullFilePath, repo.fullFileRevision);
    constexpr float headerHeight = 66.f;
    auto actions = div(ctx, mk(parent, 585010), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(32)})
        .with_flex_direction(FlexDirection::Row).with_debug_name("full_file_actions"));
    if (button(ctx, mk(actions.ent(), 5), preset::Button(text_decode::override_label(repo.fullFileEncodingOverride, repo.fullFileEncodingLabel))
            .with_size(ComponentSize{pixels(155), pixels(30)})
            .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("encoding_cycle"))) {
        repo.fullFileEncodingOverride = text_decode::next_override(repo.fullFileEncodingOverride);
        repo.fullFileCacheKey.clear();
    }
    const auto& selection = ui::diff_sel::state();
    int selectedLine = 0;
    for (const auto& line : selection.lastLines)
        if (line.ent == selection.anchor.ent && line.filePath == repo.fullFilePath) selectedLine = line.lineNo;
    int bookmarkLine = selectedLine > 0 ? selectedLine : std::max(1, repo.fullFileTargetLine);
    auto sameBookmark = [&](const CodeBookmark& bookmark) {
        return bookmark.path == repo.fullFilePath &&
               bookmark.revision == repo.fullFileRevision &&
               bookmark.line == bookmarkLine;
    };
    bool bookmarked = std::any_of(bookmarks.begin(), bookmarks.end(), sameBookmark);
    if (button(ctx, mk(actions.ent(), 4), preset::Button(bookmarked ? "Remove bookmark" : "Bookmark line " + std::to_string(bookmarkLine))
            .with_size(ComponentSize{pixels(bookmarked ? 135 : 130), pixels(30)})
            .with_debug_name("bookmark_line"))) {
        auto updated = bookmarks;
        if (bookmarked) {
            std::erase_if(updated, sameBookmark);
        } else {
            updated.push_back(CodeBookmark{
                repo.fullFilePath,
                repo.fullFileRevision,
                bookmarkLine,
                repo.fullFilePath + ":L" + std::to_string(bookmarkLine),
            });
        }
        Settings::get().set_code_bookmarks(repo.repoPath, updated);
    }
    if (selectedLine > 0 && repo.fullFileRevision != "INDEX") {
        if (button(ctx, mk(actions.ent(), 3), preset::Button("Blame line " + std::to_string(selectedLine))
                .with_size(ComponentSize{pixels(120), pixels(30)}).with_debug_name("blame_selected_line"))) {
            std::vector<std::string> args{"blame", "--line-porcelain", "-L", std::to_string(selectedLine) + "," + std::to_string(selectedLine)};
            if (!repo.fullFileRevision.empty()) args.push_back(repo.fullFileRevision);
            args.insert(args.end(), {"--", repo.fullFilePath});
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
            .with_font_size(FontSize::Small)
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
                    .with_font_size(FontSize::Small)
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_debug_name("code_bookmark"));
            ui::set_tooltip(item.ent(), label);
            if (item) {
                repo.fullFilePath = bookmark.path;
                repo.fullFileRevision = bookmark.revision;
                repo.fullFileCacheKey.clear();
                repo.fullFileTargetLine = bookmark.line;
                repo.fullFileNavigateFrames = 3;
            }
        }
    }
    if (repo.blameFuture.valid() && repo.blameFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = repo.blameFuture.get();
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
            .with_size(ComponentSize{expand(), pixels(blameHeight)}).with_font_size(FontSize::Small)
            .with_text_overflow(afterhours::ui::TextOverflow::Wrap).with_debug_name("blame_attribution"));
        if (button(ctx, mk(panel.ent(), 1), preset::Button("Close")
                .with_size(ComponentSize{pixels(65), pixels(30)}))) repo.blameOpen = false;
    }
    if (repo.fullFileFuture.valid()) {
        div(ctx, mk(parent, 585003), ComponentConfig{}
            .with_label("Loading file...").with_size(ComponentSize{percent(1.f), pixels(40)})
            .with_font_size(FontSize::Medium).with_debug_name("full_file_loading"));
    } else if (!repo.fullFileError.empty()) {
        div(ctx, mk(parent, 585001), ComponentConfig{}
            .with_label(repo.fullFileError).with_size(ComponentSize{percent(1.f), pixels(100)})
            .with_font_size(FontSize::Medium).with_text_overflow(afterhours::ui::TextOverflow::Wrap)
            .with_debug_name("full_file_error"));
    } else {
        ui::render_diff(ctx, parent, repo.fullFileDiff, layout.mainContent.width,
                        layout.mainContent.height - headerHeight - bookmarkHeight - blameHeight, false, changed, false,
                        repo.repoPath, nullptr, "file:" + repo.fullFileRevision);
    }
}

}
