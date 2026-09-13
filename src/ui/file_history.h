#pragma once

#include "../ecs/ui_imports.h"
#include "../git/git_parser.h"

namespace ecs {

inline void load_file_history(RepoComponent& repo) {
    repo.fileHistoryError.clear();
    repo.fileHistoryFutureStamp = navigation::stamp(repo, repo.fileHistoryPath + "\n" + repo.fileHistoryRevision + ":" + std::to_string(repo.fileHistoryLimit));
    repo.fileHistoryFuture = git::git_run_async(repo.repoPath,
        {"log", "--follow", "-n", std::to_string(repo.fileHistoryLimit),
         "--format=%H%x00%h%x00%s%x00%an%x00%aI%x00%D%x00%P",
         repo.fileHistoryRevision, "--", repo.fileHistoryPath});
}

inline void open_file_history(RepoComponent& repo, const std::string& path,
                               const std::string& revision = "HEAD") {
    repo.fileHistoryPath = path;
    repo.fileHistoryRevision = revision.empty() || revision == "INDEX" ? "HEAD" : revision;
    repo.fileHistoryLimit = 200;
    repo.fileHistoryEntries.clear();
    repo.fileHistoryOpen = true;
    load_file_history(repo);
}

inline void render_file_history(UIContext<InputAction>& ctx, Entity& parent,
                                 RepoComponent& repo, LayoutComponent& layout) {
    using namespace std::chrono_literals;
    if (repo.fileHistoryFuture.valid() && repo.fileHistoryFuture.wait_for(0s) == std::future_status::ready) {
        auto result = repo.fileHistoryFuture.get();
        if (!navigation::accepts(repo, repo.fileHistoryFutureStamp, repo.fileHistoryPath + "\n" + repo.fileHistoryRevision + ":" + std::to_string(repo.fileHistoryLimit))) return;
        repo.fileHistoryFuture = {};
        if (result.success()) repo.fileHistoryEntries = git::parse_log(result.stdout_str());
        else repo.fileHistoryError = result.stderr_str();
    }
    auto header = div(ctx, mk(parent, 588000), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(34)}).with_flex_direction(FlexDirection::Row));
    if (button(ctx, mk(header.ent(), 0), preset::Button("Back")
            .with_size(ComponentSize{pixels(65), pixels(30)}))) repo.fileHistoryOpen = false;
    div(ctx, mk(header.ent(), 1), ComponentConfig{}
        .with_label("History · " + repo.fileHistoryPath + " · follows renames")
        .with_size(ComponentSize{expand(), pixels(30)}).with_font_size(FontSize::Medium));
    std::string status = repo.fileHistoryFuture.valid() ? "Loading history..." :
        std::to_string(repo.fileHistoryEntries.size()) + " commits";
    if (!repo.fileHistoryError.empty()) status = repo.fileHistoryError;
    div(ctx, mk(parent, 588001), ComponentConfig{}.with_label(status)
        .with_size(ComponentSize{percent(1.f), pixels(28)}).with_font_size(FontSize::Small));
    afterhours::ui::imm::virtual_list(ctx, mk(parent, 588002), repo.fileHistoryEntries.size(), 36.f,
        [&](size_t i, Entity& row) {
            const auto& commit = repo.fileHistoryEntries[i];
            if (button(ctx, mk(row, 0), preset::Button(commit.shortHash + "  " + commit.subject + "  · " + commit.author + " · " + commit.authorDate.substr(0, 10))
                    .with_size(ComponentSize{percent(1.f), pixels(36)}).with_alignment(TextAlignment::Left)
                    .with_custom_background(theme::PANEL_BG).with_font_size(FontSize::Medium)
                    .with_debug_name("file_history_commit:" + std::to_string(i)))) {
                navigation::click(repo, reading::review(commit.hash), afterhours::input::is_key_pressed(257), reading::ClickRegion::Search);
            }
        }, ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(std::max(40.f, layout.mainContent.height - 100.f))}));
    if (!repo.fileHistoryFuture.valid() && (static_cast<int>(repo.fileHistoryEntries.size()) == repo.fileHistoryLimit || !repo.fileHistoryError.empty())) {
        if (button(ctx, mk(parent, 588003), preset::Button(repo.fileHistoryError.empty() ? "Load more history" : "Retry")
                .with_size(ComponentSize{pixels(180), pixels(30)}))) {
            if (repo.fileHistoryError.empty()) repo.fileHistoryLimit += 200;
            load_file_history(repo);
        }
    }
}

}
