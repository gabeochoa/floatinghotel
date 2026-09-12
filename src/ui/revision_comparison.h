#pragma once

#include "diff_renderer.h"
#include "../git/git_parser.h"

namespace ecs {

inline void render_revision_comparison(UIContext<InputAction>& ctx, Entity& parent,
                                        RepoComponent& repo, LayoutComponent& layout, ReviewComponent* review = nullptr) {
    using namespace std::chrono_literals;
    bool changed = false;
    if (!repo.comparisonScope.empty() && !repo.comparisonFuture.valid() &&
        (repo.comparisonNeedsLoad || repo.comparisonContext != repo.diffContext || repo.comparisonIgnoreWhitespace != repo.ignoreWhitespace)) {
        auto [base, target] = diff_revisions(repo.comparisonScope);
        repo.comparisonFuture = git::git_compare_async(repo.repoPath, base, target, false,
            repo.diffContext, repo.ignoreWhitespace);
        repo.comparisonContext = repo.diffContext;
        repo.comparisonIgnoreWhitespace = repo.ignoreWhitespace;
        repo.comparisonNeedsLoad = false;
    }
    if (repo.comparisonFuture.valid() && repo.comparisonFuture.wait_for(0s) == std::future_status::ready) {
        auto result = repo.comparisonFuture.get();
        repo.comparisonFuture = {};
        if (result.patch.success()) {
            repo.comparisonDiff = git::parse_diff(result.patch.stdout_str());
            repo.comparisonScope = "compare:" + result.base + ":" + result.target;
            changed = true;
        } else repo.comparisonError = result.patch.stderr_str().empty() ? "Unable to compare revisions; they may have no common ancestor." : result.patch.stderr_str();
    }
    auto field = [&](int id, const std::string& label, std::string& text, const std::string& name) {
        auto row = div(ctx, mk(parent, id), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(34)}).with_flex_direction(FlexDirection::Row));
        div(ctx, mk(row.ent(), 0), ComponentConfig{}.with_label(label)
            .with_size(ComponentSize{pixels(100), pixels(32)}).with_font_size(FontSize::Small));
        afterhours::text_input::text_input(ctx, mk(row.ent(), 1), text, ComponentConfig{}
            .with_size(ComponentSize{pixels(std::max(80.f, layout.mainContent.width - 110.f)), pixels(32)}).with_debug_name(name));
    };
    field(590000, "Base revision", repo.comparisonBase, "compare_base");
    field(590001, "Target revision", repo.comparisonTarget, "compare_target");
    auto actions = div(ctx, mk(parent, 590002), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(34)}).with_flex_direction(FlexDirection::Row));
    if (button(ctx, mk(actions.ent(), 0), preset::Button(repo.comparisonMergeBase ? "Merge base: on" : "Merge base: off")
            .with_size(ComponentSize{pixels(150), pixels(30)}).with_debug_name("compare_merge_base")))
        repo.comparisonMergeBase = !repo.comparisonMergeBase;
    if (button(ctx, mk(actions.ent(), 1), preset::Button("Compare")
            .with_size(ComponentSize{pixels(90), pixels(30)}).with_debug_name("compare_submit"))) {
        repo.comparisonFuture = {};
        repo.comparisonError.clear();
        repo.comparisonDiff.clear();
        repo.comparisonScope.clear();
        repo.comparisonNeedsLoad = false;
        repo.comparisonContext = repo.diffContext;
        repo.comparisonIgnoreWhitespace = repo.ignoreWhitespace;
        if (repo.comparisonBase.empty() || repo.comparisonTarget.empty()) repo.comparisonError = "Enter both revisions (branch, tag, or commit).";
        else repo.comparisonFuture = git::git_compare_async(repo.repoPath, repo.comparisonBase, repo.comparisonTarget,
            repo.comparisonMergeBase, repo.diffContext, repo.ignoreWhitespace);
    }
    if (button(ctx, mk(actions.ent(), 2), preset::Button("Close")
            .with_size(ComponentSize{pixels(65), pixels(30)}))) repo.comparisonOpen = false;
    auto status = repo.comparisonFuture.valid() ? "Comparing revisions..." : repo.comparisonScope.empty() ? "Choose revisions to compare" :
        "Resolved revisions: " + diff_revisions(repo.comparisonScope).first.substr(0, 12) + " → " + diff_revisions(repo.comparisonScope).second.substr(0, 12);
    if (!repo.comparisonError.empty()) status = repo.comparisonError;
    div(ctx, mk(parent, 590003), ComponentConfig{}.with_label(status)
        .with_size(ComponentSize{percent(1.f), pixels(30)}).with_font_size(FontSize::Small)
        .with_text_overflow(afterhours::ui::TextOverflow::Wrap));
    if (!repo.comparisonScope.empty())
        ui::render_diff(ctx, parent, repo.comparisonDiff, layout.mainContent.width,
            layout.mainContent.height - 132.f, false, changed,
            layout.diffViewMode == LayoutComponent::DiffViewMode::SideBySide, repo.repoPath, review, repo.comparisonScope);
}

}
