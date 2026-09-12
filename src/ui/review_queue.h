#pragma once

#include "../ecs/ui_imports.h"
#include "../git/git_parser.h"
#include <afterhours/src/plugins/toast.h>

namespace ecs {

inline void poll_review_queue(RepoComponent& repo, ReviewComponent& review) {
    if (repo.reviewQueueScope.empty() || review.storageScope != repo.reviewQueueScope) return;
    if (!repo.reviewQueueFuture.valid() || repo.reviewQueueFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto result = repo.reviewQueueFuture.get();
    repo.reviewQueueFuture = {};
    if (!result.success()) {
        repo.reviewQueueError = "Unable to load review range: " + result.stderr_str();
        return;
    }
    review.queue = refreshed_review_queue(review.queue, git::parse_log(result.stdout_str()));
    if (review.queue.commits.empty()) {
        repo.reviewQueueError = "No commits in this range";
        return;
    }
    repo.selectedCommitHash = review.queue.commits[review.queue.position].hash;
    repo.comparisonOpen = false;
    review.reviewing = true;
    review.dirty = true;
}

inline void close_review_queue(RepoComponent& repo) {
    repo.reviewQueueScope.clear();
    repo.reviewQueueFuture = {};
    repo.reviewQueueError.clear();
    repo.selectedCommitHash.clear();
}

inline void render_review_queue(UIContext<InputAction>& ctx, Entity& parent, int id,
        RepoComponent& repo, ReviewComponent& review, const CommitDetailCache& cache) {
    if (repo.reviewQueueScope.empty() || review.queue.commits.empty()) return;
    auto& queue = review.queue;
    auto row = div(ctx, mk(parent, id), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(32)}).with_flex_direction(FlexDirection::Row)
        .with_debug_name("review_queue"));
    auto select = [&] {
        repo.selectedCommitHash = queue.commits[queue.position].hash;
        review.dirty = true;
    };
    bool active = repo.selectedCommitHash == queue.commits[queue.position].hash;
    div(ctx, mk(row.ent(), 0), ComponentConfig{}
        .with_label(active ? "Queue " + std::to_string(queue.position + 1) + "/" + std::to_string(queue.commits.size()) +
            " · " + std::to_string(queue.completed.size()) + " reviewed" : "Browsing outside review queue")
        .with_size(ComponentSize{expand(), pixels(28)}).with_font_size(FontSize::Small)
        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("review_queue_position"));
    if (!active) {
        if (button(ctx, mk(row.ent(), 1), preset::Button("Resume queue")
                .with_size(ComponentSize{children(), pixels(28)}).with_debug_name("review_queue_resume"))) select();
    } else {
        if (button(ctx, mk(row.ent(), 1), preset::Button("Previous")
                .with_size(ComponentSize{children(), pixels(28)}).with_font_size(FontSize::Small).with_debug_name("review_queue_previous")) && queue.position > 0) {
            --queue.position;
            select();
        }
        if (button(ctx, mk(row.ent(), 2), preset::Button("Next")
                .with_size(ComponentSize{children(), pixels(28)}).with_font_size(FontSize::Small).with_debug_name("review_queue_next")) && queue.position + 1 < queue.commits.size()) {
            ++queue.position;
            select();
        }
        if (button(ctx, mk(row.ent(), 3), preset::Button("Reviewed and next")
                .with_size(ComponentSize{children(), pixels(28)}).with_font_size(FontSize::Small).with_debug_name("review_queue_complete"))) {
            if (cache.cachedCommitHash != repo.selectedCommitHash || cache.patchFuture.valid() ||
                cache.infoFuture.valid() || !cache.commitDetailError.empty() ||
                current_review_verdict(review, repo.selectedCommitHash, cache.commitDetailDiff) == ReviewVerdict::InProgress)
                afterhours::toast::send_info(ctx, "Finish this commit's review before completing it", 2.f);
            else {
                queue.completed.insert(repo.selectedCommitHash);
                if (queue.position + 1 < queue.commits.size()) ++queue.position;
                select();
            }
        }
    }
    if (button(ctx, mk(row.ent(), 4), preset::Button("Exit queue")
            .with_size(ComponentSize{children(), pixels(28)}).with_font_size(FontSize::Small).with_debug_name("review_queue_exit"))) close_review_queue(repo);
}

}
