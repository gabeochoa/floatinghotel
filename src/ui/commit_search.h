#pragma once

#include "../ecs/ui_imports.h"
#include "../git/git_parser.h"

namespace ecs {

inline void load_commit_search(RepoComponent& repo) {
    repo.commitSearchError.clear();
    auto args = git::history_search_args(repo.commitSearchQuery, repo.commitSearchLimit);
    if (!args) { repo.commitSearchError = "Use valid YYYY-MM-DD dates, with Since before Until."; return; }
    repo.commitSearchFuture = git::git_run_async(repo.repoPath, *args);
}

inline void render_commit_search(UIContext<InputAction>& ctx, Entity& parent,
                                  RepoComponent& repo, LayoutComponent& layout) {
    using namespace std::chrono_literals;
    if (repo.commitSearchFuture.valid() && repo.commitSearchFuture.wait_for(0s) == std::future_status::ready) {
        auto result = repo.commitSearchFuture.get();
        repo.commitSearchFuture = {};
        if (result.success()) repo.commitSearchEntries = git::parse_log(result.stdout_str());
        else repo.commitSearchError = result.stderr_str();
    }
    div(ctx, mk(parent, 589000), ComponentConfig{}
        .with_label("Search commits · all refs · case-insensitive text · all filters combined")
        .with_size(ComponentSize{percent(1.f), pixels(30)}).with_font_size(FontSize::Medium));
    int id = 589001;
    auto field = [&](const std::string& label, std::string& value, const std::string& name) {
        auto row = div(ctx, mk(parent, id++), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(34)}).with_flex_direction(FlexDirection::Row));
        div(ctx, mk(row.ent(), 0), ComponentConfig{}.with_label(label)
            .with_size(ComponentSize{pixels(160), pixels(32)}).with_font_size(FontSize::Small));
        afterhours::text_input::text_input(ctx, mk(row.ent(), 1), value, ComponentConfig{}
            .with_size(ComponentSize{pixels(std::max(80.f, layout.mainContent.width - 170.f)), pixels(32)}).with_debug_name(name));
    };
    field("Message contains", repo.commitSearchQuery.message, "commit_search_message");
    field("Author contains", repo.commitSearchQuery.author, "commit_search_author");
    field("Since (YYYY-MM-DD)", repo.commitSearchQuery.since, "commit_search_since");
    field("Until (YYYY-MM-DD)", repo.commitSearchQuery.until, "commit_search_until");
    field("File or directory path", repo.commitSearchQuery.path, "commit_search_path");
    auto actions = div(ctx, mk(parent, id++), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(34)}).with_flex_direction(FlexDirection::Row));
    if (button(ctx, mk(actions.ent(), 0), preset::Button("Search commits")
            .with_size(ComponentSize{pixels(140), pixels(30)}).with_debug_name("commit_search_submit")) ||
        afterhours::input::is_key_pressed(257)) {
        repo.commitSearchLimit = 200;
        repo.commitSearchEntries.clear();
        load_commit_search(repo);
    }
    if (button(ctx, mk(actions.ent(), 1), preset::Button("Close")
            .with_size(ComponentSize{pixels(70), pixels(30)}))) repo.commitSearchOpen = false;
    if (!repo.commitSearchFuture.valid() && static_cast<int>(repo.commitSearchEntries.size()) == repo.commitSearchLimit) {
        if (button(ctx, mk(actions.ent(), 2), preset::Button("Load more")
                .with_size(ComponentSize{pixels(100), pixels(30)}))) {
            repo.commitSearchLimit += 200;
            load_commit_search(repo);
        }
    }
    auto status = repo.commitSearchFuture.valid() ? "Searching..." : std::to_string(repo.commitSearchEntries.size()) + " matching commits";
    if (!repo.commitSearchError.empty()) status = repo.commitSearchError;
    div(ctx, mk(parent, id++), ComponentConfig{}.with_label(status)
        .with_size(ComponentSize{percent(1.f), pixels(28)}).with_font_size(FontSize::Small));
    afterhours::ui::imm::virtual_list(ctx, mk(parent, id++), repo.commitSearchEntries.size(), 34.f,
        [&](size_t i, Entity& row) {
            const auto& commit = repo.commitSearchEntries[i];
            if (button(ctx, mk(row, 0), preset::Button(commit.shortHash + "  " + commit.subject + " · " + commit.author + " · " + commit.authorDate.substr(0, 10))
                    .with_size(ComponentSize{percent(1.f), pixels(34)}).with_alignment(TextAlignment::Left)
                    .with_custom_background(theme::PANEL_BG).with_font_size(FontSize::Medium)
                    .with_debug_name("commit_search_result:" + std::to_string(i)))) {
                repo.selectedCommitHash = commit.hash;
                repo.selectedFilePath.clear();
                repo.fullFilePath.clear();
                repo.commitSearchOpen = false;
            }
        }, ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(std::max(40.f, layout.mainContent.height - 270.f))}));
}

}
