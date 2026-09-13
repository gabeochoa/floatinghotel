#pragma once

#include "diff_renderer.h"
#include "range_diff.h"
#include "../git/git_parser.h"

namespace ecs {

inline void render_revision_comparison(UIContext<InputAction>& ctx, Entity& parent,
                                        RepoComponent& repo, LayoutComponent& layout, ReviewComponent* review = nullptr) {
    using namespace std::chrono_literals;
    if (button(ctx, mk(parent, 598000), preset::Button(repo.rangeDiff.enabled ? "Compare revisions" : "Compare commit series")
            .with_size(ComponentSize{pixels(240), pixels(30)}).with_debug_name("comparison_mode"))) {
        repo.rangeDiff.enabled = !repo.rangeDiff.enabled;
        repo.rangeDiff.future = {};
        if (repo.rangeDiff.enabled) repo.comparisonFuture = {};
    }
    if (repo.rangeDiff.enabled) {
        render_range_diff(ctx, parent, repo, layout);
        return;
    }
    bool changed = false;
    if (!repo.comparisonScope().empty() && !repo.comparisonFuture.valid() &&
        (repo.comparisonNeedsLoad || repo.comparisonContext != repo.diffContext || repo.comparisonIgnoreWhitespace != repo.ignoreWhitespace)) {
        auto [base, target] = diff_revisions(repo.comparisonScope());
        repo.comparisonRequest = RepoComponent::ComparisonRequest::Document;
        repo.comparisonRequestStamp = navigation::stamp(repo, navigation::comparison_request_key(repo));
        repo.comparisonFuture = git::git_compare_async(repo.repoPath, base, target, false,
            repo.diffContext, repo.ignoreWhitespace);
        repo.comparisonContext = repo.diffContext;
        repo.comparisonIgnoreWhitespace = repo.ignoreWhitespace;
        repo.comparisonNeedsLoad = false;
    }
    if (repo.comparisonFuture.valid() && repo.comparisonFuture.wait_for(0s) == std::future_status::ready) {
        auto result = repo.comparisonFuture.get();
        repo.comparisonFuture = {};
        if (!navigation::accepts_comparison(repo, repo.comparisonRequestStamp)) return;
        if (result.patch.success()) {
            if (!navigation::complete_comparison(repo, repo.comparisonRequestStamp, result.base, result.target)) return;
            repo.comparisonDiff = git::parse_diff(result.patch.stdout_str());
            navigation::remember_review_files(repo, repo.comparisonDiff);
            changed = true;
        } else repo.comparisonError = result.patch.stderr_str().empty() ? "Unable to compare revisions; they may have no common ancestor." : result.patch.stderr_str();
    }
    ui::diff_syntax::update(repo, repo.comparisonDiff, repo.comparisonScope());
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
        navigation::comparison_editor(repo);
        repo.comparisonLoadedScope.clear();
        repo.comparisonNeedsLoad = false;
        repo.comparisonContext = repo.diffContext;
        repo.comparisonIgnoreWhitespace = repo.ignoreWhitespace;
        if (repo.comparisonBase.empty() || repo.comparisonTarget.empty()) repo.comparisonError = "Enter both revisions (branch, tag, or commit).";
        else {
            repo.comparisonRequest = RepoComponent::ComparisonRequest::SubmittedForm;
            repo.comparisonRequestStamp = navigation::stamp(repo, navigation::comparison_request_key(repo));
            repo.comparisonFuture = git::git_compare_async(repo.repoPath, repo.comparisonBase, repo.comparisonTarget,
                repo.comparisonMergeBase, repo.diffContext, repo.ignoreWhitespace);
        }
    }
    if (button(ctx, mk(actions.ent(), 2), preset::Button("Close")
            .with_size(ComponentSize{pixels(65), pixels(30)}))) navigation::open(repo, reading::review("wt"));
    if (review && !repo.comparisonScope().empty() && button(ctx, mk(actions.ent(), 3), preset::Button("Review commits")
            .with_size(ComponentSize{pixels(140), pixels(30)}).with_debug_name("review_range_commits"))) {
        auto [base, target] = diff_revisions(repo.comparisonScope());
        repo.reviewQueueScope = "queue:" + base + ":" + target;
        repo.reviewQueueError.clear();
        repo.reviewQueueFuture = git::git_run_async(repo.repoPath, {"log", "--reverse", "--topo-order",
            "--format=%H%x00%h%x00%s%x00%an%x00%aI%x00%D%x00%P", base + ".." + target, "--"});
        navigation::open(repo, reading::review("wt"));
        repo.reviewQueueFutureStamp = navigation::stamp(repo, repo.reviewQueueScope);
    }
    auto status = repo.comparisonFuture.valid() ? "Comparing revisions..." : repo.comparisonScope().empty() ? "Choose revisions to compare" :
        "Resolved revisions: " + diff_revisions(repo.comparisonScope()).first.substr(0, 12) + " → " + diff_revisions(repo.comparisonScope()).second.substr(0, 12);
    if (!repo.comparisonError.empty()) status = repo.comparisonError;
    div(ctx, mk(parent, 590003), ComponentConfig{}.with_label(status)
        .with_size(ComponentSize{percent(1.f), pixels(30)}).with_font_size(FontSize::Small)
        .with_text_overflow(afterhours::ui::TextOverflow::Wrap));
    if (!repo.comparisonScope().empty())
        ui::render_diff(ctx, parent, repo.comparisonDiff, layout.mainContent.width,
            layout.mainContent.height - 162.f, false, changed,
            layout.diffViewMode == LayoutComponent::DiffViewMode::SideBySide, repo.repoPath, review, repo.comparisonScope());
}

}
