#pragma once

#include "diff_renderer.h"
#include "../review_snapshot.h"
#include "../review_store.h"
#include "../git/git_parser.h"
#include <nlohmann/json.hpp>
#include <ctime>

namespace app_state { extern bool testModeEnabled; }

namespace ecs {

inline void start_review_snapshot(RepoComponent& repo, ReviewComponent& review, bool capture) {
    if (review.snapshotFuture.valid()) return;
    review.snapshotError.clear();
    review.snapshotCapturing = capture;
    review.snapshotContext = repo.diffContext;
    review.snapshotIgnoreWhitespace = repo.ignoreWhitespace;
    auto path = capture ? review_store::review_path(repo.repoPath, review.storageScope) + ".baseline.cbor" : review.baselineSnapshot;
    if (capture && app_state::testModeEnabled) path = repo.repoPath + "/.git/floatinghotel-baseline.cbor";
    review.snapshotFuture = review_store::snapshot_async(repo.repoPath, path, repo.headCommitHash,
        capture, repo.diffContext, repo.ignoreWhitespace);
}

inline void poll_review_snapshot(ReviewComponent& review) {
    if (!review.snapshotFuture.valid() || review.snapshotFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto result = review.snapshotFuture.get();
    review.snapshotFuture = {};
    if (!result.success()) { review.snapshotError = result.stderr_str(); return; }
    if (review.snapshotCapturing) {
        const auto saved = nlohmann::json::parse(result.stdout_str());
        review.baselineSnapshot = saved.at("path").get<std::string>();
        review.baselineHead = saved.at("head").get<std::string>();
        review.baselineCapturedAt = saved.at("captured_at").get<int64_t>();
        review.dirty = true;
    } else {
        review.sinceReviewDiff.clear();
        for (const auto& entry : nlohmann::json::parse(result.stdout_str())) {
            auto diffs = git::parse_diff(entry.at("patch").get<std::string>());
            for (auto& diff : diffs) {
                diff.filePath = entry.at("file").get<std::string>();
                diff.oldPath = diff.filePath;
                review.sinceReviewDiff.push_back(std::move(diff));
            }
        }
    }
}

inline std::string baseline_label(const ReviewComponent& review) {
    std::string label = review.baselineHead.empty() ? "Unborn HEAD" : review.baselineHead.substr(0, 8);
    if (review.baselineCapturedAt <= 0) return label + " · capture time unavailable";
    const auto captured = static_cast<std::time_t>(review.baselineCapturedAt);
    std::tm time{};
    gmtime_r(&captured, &time);
    char text[32]{};
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S UTC", &time);
    return label + " · " + text;
}

inline void open_saved_review(RepoComponent& repo, ReviewComponent& review) {
    if (review.baselineSnapshot.empty() || review.snapshotFuture.valid()) return;
    review.sinceReviewOpen = true;
    repo.reading.rows.clear();
    if (auto* layout = find_singleton<LayoutComponent>()) layout->readingPanelCollapsed = false;
    start_review_snapshot(repo, review, false);
}

inline void render_review_snapshot(UIContext<InputAction>& ctx, Entity& parent,
    RepoComponent& repo, ReviewComponent& review, LayoutComponent& layout) {
    if (!review.snapshotFuture.valid() && (review.snapshotContext != repo.diffContext ||
        review.snapshotIgnoreWhitespace != repo.ignoreWhitespace)) start_review_snapshot(repo, review, false);
    auto actions = div(ctx, mk(parent, 592000), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(30)}).with_flex_direction(FlexDirection::Row));
    div(ctx, mk(actions.ent(), 0), ComponentConfig{}.with_label("Changes since saved review")
        .with_size(ComponentSize{expand(), pixels(28)}).with_font_size(FontSize::Medium));
    if (!review.snapshotFuture.valid() && button(ctx, mk(actions.ent(), 1), preset::Button("Refresh")
        .with_size(ComponentSize{pixels(75), pixels(28)}))) start_review_snapshot(repo, review, false);
    if (button(ctx, mk(actions.ent(), 2), preset::Button("Close")
        .with_size(ComponentSize{pixels(65), pixels(28)}).with_debug_name("snapshot_close"))) {
        review.sinceReviewOpen = false;
        navigation::restore_anchor(repo);
    }
    auto identity = div(ctx, mk(parent, 592003), ComponentConfig{}.with_label(baseline_label(review))
        .with_size(ComponentSize{percent(1.f), pixels(24)}).with_font_size(pixels(12))
        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("snapshot_baseline_identity"));
    ui::set_tooltip(identity.ent(), baseline_label(review));
    if (review.snapshotFuture.valid() || !review.snapshotError.empty()) {
        div(ctx, mk(parent, 592001), ComponentConfig{}.with_label(review.snapshotFuture.valid() ? "Comparing saved contents..." : review.snapshotError)
            .with_size(ComponentSize{percent(1.f), pixels(60)}).with_text_overflow(afterhours::ui::TextOverflow::Wrap));
    } else if (review.sinceReviewDiff.empty()) {
        div(ctx, mk(parent, 592002), ComponentConfig{}.with_label("No changes since saved review")
            .with_size(ComponentSize{percent(1.f), pixels(40)}));
    } else ui::render_diff(ctx, parent, review.sinceReviewDiff, layout.mainContent.width,
        layout.mainContent.height - 54.f, false, false,
        layout.diffViewMode == LayoutComponent::DiffViewMode::SideBySide, repo.repoPath, nullptr, "snapshot");
}

}
