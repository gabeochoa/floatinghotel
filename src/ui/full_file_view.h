#pragma once

#include "diff_renderer.h"
#include "file_history.h"
#include "../git/content_reader.h"

namespace ecs {

inline void render_full_file(UIContext<InputAction>& ctx, Entity& parent,
                             RepoComponent& repo, LayoutComponent& layout) {
    std::string key = repo.repoPath + "\n" + repo.fullFileRevision + "\n" + repo.fullFilePath;
    if (repo.fullFileRevision.empty()) key += ":" + std::to_string(repo.dataGeneration);
    bool changed = repo.fullFileCacheKey != key;
    if (changed) {
        repo.fullFileCacheKey = key;
        repo.fullFileDiff.clear();
        repo.fullFileError.clear();
        repo.fullFileBytes.clear();
        repo.blameOpen = false;
        repo.blameFuture = {};
        repo.fullFileFuture = git::read_file_async({repo.repoPath, repo.fullFilePath, repo.fullFileRevision});
    }
    if (repo.fullFileFuture.valid() && repo.fullFileFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto content = repo.fullFileFuture.get();
        repo.fullFileError = std::move(content.error);
        repo.fullFileBytes = std::move(content.raw);
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
        .with_debug_name("full_file_revision"));
    if (button(ctx, mk(header.ent(), 2), preset::Button("History")
            .with_size(ComponentSize{pixels(75), pixels(30)}).with_debug_name("file_history_open")))
        open_file_history(repo, repo.fullFilePath, repo.fullFileRevision);
    const auto& selection = ui::diff_sel::state();
    int selectedLine = 0;
    for (const auto& line : selection.lastLines)
        if (line.ent == selection.anchor.ent && line.filePath == repo.fullFilePath) selectedLine = line.lineNo;
    if (selectedLine > 0 && repo.fullFileRevision != "INDEX") {
        if (button(ctx, mk(header.ent(), 3), preset::Button("Blame line " + std::to_string(selectedLine))
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
                        layout.mainContent.height - 34.f - blameHeight, false, changed, false,
                        repo.repoPath, nullptr, "file:" + repo.fullFileRevision);
    }
}

}
