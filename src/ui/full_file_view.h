#pragma once

#include <fstream>
#include <filesystem>
#include <iterator>
#include <sstream>
#include "diff_renderer.h"
#include "file_history.h"
#include "../git/git_runner.h"

namespace ecs {

inline FileDiff full_file_diff(const std::string& path, const std::string& content) {
    FileDiff file;
    file.filePath = path;
    file.isFullContent = true;
    file.isBinary = content.find('\0') != std::string::npos;
    if (file.isBinary) return file;
    DiffHunk hunk;
    hunk.oldStart = hunk.newStart = 1;
    hunk.header = "Complete file";
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) hunk.lines.push_back(" " + line);
    hunk.oldCount = hunk.newCount = static_cast<int>(hunk.lines.size());
    if (!content.empty() && !content.ends_with('\n')) hunk.noNewline.insert(hunk.lines.size() - 1);
    file.hunks.push_back(std::move(hunk));
    return file;
}

inline void render_full_file(UIContext<InputAction>& ctx, Entity& parent,
                             RepoComponent& repo, LayoutComponent& layout) {
    std::string key = repo.repoPath + "\n" + repo.fullFileRevision + "\n" + repo.fullFilePath;
    if (repo.fullFileRevision.empty()) key += ":" + std::to_string(repo.dataGeneration);
    bool changed = repo.fullFileCacheKey != key;
    if (changed) {
        repo.fullFileCacheKey = key;
        repo.fullFileDiff.clear();
        repo.fullFileError.clear();
        std::string content;
        if (repo.fullFileRevision.empty()) {
            std::ifstream input(std::filesystem::path(repo.repoPath) / repo.fullFilePath, std::ios::binary);
            if (!input) repo.fullFileError = "Unable to read working-tree file";
            else content.assign(std::istreambuf_iterator<char>(input), {});
        } else {
            std::string spec = repo.fullFileRevision == "INDEX" ? ":" + repo.fullFilePath
                              : repo.fullFileRevision + ":" + repo.fullFilePath;
            auto result = git::git_run(repo.repoPath, {"show", spec});
            if (result.success()) content = result.stdout_str();
            else repo.fullFileError = result.stderr_str();
        }
        if (repo.fullFileError.empty()) repo.fullFileDiff.push_back(full_file_diff(repo.fullFilePath, content));
    }
    auto header = div(ctx, mk(parent, 585000), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(34)})
        .with_flex_direction(FlexDirection::Row).with_debug_name("full_file_header"));
    if (button(ctx, mk(header.ent(), 0), preset::Button("Back to diff")
            .with_size(ComponentSize{pixels(110), pixels(30)}).with_debug_name("full_file_back")))
        repo.fullFilePath.clear();
    div(ctx, mk(header.ent(), 1), ComponentConfig{}
        .with_label(repo.fullFilePath + " @ " + (repo.fullFileRevision.empty() ? "working tree" : repo.fullFileRevision))
        .with_size(ComponentSize{expand(), pixels(30)}).with_font_size(FontSize::Small)
        .with_debug_name("full_file_revision"));
    if (button(ctx, mk(header.ent(), 2), preset::Button("History")
            .with_size(ComponentSize{pixels(75), pixels(30)}).with_debug_name("file_history_open")))
        open_file_history(repo, repo.fullFilePath, repo.fullFileRevision);
    if (!repo.fullFileError.empty()) {
        div(ctx, mk(parent, 585001), ComponentConfig{}
            .with_label(repo.fullFileError).with_size(ComponentSize{percent(1.f), pixels(100)})
            .with_font_size(FontSize::Medium).with_text_overflow(afterhours::ui::TextOverflow::Wrap)
            .with_debug_name("full_file_error"));
    } else {
        ui::render_diff(ctx, parent, repo.fullFileDiff, layout.mainContent.width,
                        layout.mainContent.height - 34.f, false, changed, false,
                        repo.repoPath, nullptr, "file:" + repo.fullFileRevision);
    }
}

}
