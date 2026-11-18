#pragma once

#include "../ecs/ui_imports.h"
#include "../git/git_parser.h"
#include <afterhours/src/plugins/toast.h>

namespace ecs {

inline void poll_review_queue(RepoComponent& repo, ReviewComponent& review) {
    if (repo.reviewQueue.key.empty() || review.storageScope != repo.reviewQueue.key) return;
    const bool wasPending = repo.reviewQueue.busy();
    auto result = repo.reviewQueue.poll(repo);
    if (!result) {
        if (wasPending && repo.reviewQueue.phase == async_slot::Phase::Idle)
            repo.reviewQueue.error = "Review queue navigation was superseded; reopen the range to retry";
        return;
    }
    if (!result->success()) {
        repo.reviewQueue.error = "Unable to load review range: " + result->stderr_str();
        return;
    }
    review.queue = refreshed_review_queue(review.queue, git::parse_log(result->stdout_str()));
    if (review.queue.commits.empty()) {
        repo.reviewQueue.error = "No commits in this range";
        return;
    }
    navigation::open(repo, reading::review(review.queue.commits[review.queue.position].hash), true);
    review.reviewing = true;
    review.mark_dirty();
}

inline void close_review_queue(RepoComponent& repo) {
    repo.reviewQueue.invalidate();
    navigation::open(repo, reading::review("wt"));
}

inline void render_review_queue(UIContext<InputAction>& ctx, Entity& parent, int id,
        RepoComponent& repo, ReviewComponent& review, const CommitDetailCache& cache) {
    if (repo.reviewQueue.key.empty() || review.queue.commits.empty()) return;
    auto& queue = review.queue;
    auto row = div(ctx, mk(parent, id), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(32)}).with_flex_direction(FlexDirection::Row)
        .with_debug_name("review_queue"));
    auto select = [&] {
        navigation::open(repo, reading::review(queue.commits[queue.position].hash), true);
        review.mark_dirty();
    };
    bool active = repo.selectedCommitHash() == queue.commits[queue.position].hash;
    if (review_queue_completion_is_stale(review, repo, cache) && queue.completed.erase(repo.selectedCommitHash()) > 0)
        review.mark_dirty();
    div(ctx, mk(row.ent(), 0), ComponentConfig{}
        .with_label(active ? "Queue " + std::to_string(queue.position + 1) + "/" + std::to_string(queue.commits.size()) +
            " · " + std::to_string(queue.completed.size()) + " reviewed" : "Browsing outside review queue")
        .with_size(ComponentSize{expand(), pixels(28)}).with_font_size(pixels(12))
        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("review_queue_position"));
    if (!active) {
        if (button(ctx, mk(row.ent(), 1), preset::Button("Resume queue")
                .with_size(ComponentSize{children(), pixels(28)}).with_debug_name("review_queue_resume"))) select();
    } else {
        if (button(ctx, mk(row.ent(), 1), preset::Button("Previous")
                .with_size(ComponentSize{children(), pixels(28)}).with_font_size(pixels(12)).with_debug_name("review_queue_previous")) && queue.position > 0) {
            --queue.position;
            select();
        }
        if (button(ctx, mk(row.ent(), 2), preset::Button("Next")
                .with_size(ComponentSize{children(), pixels(28)}).with_font_size(pixels(12)).with_debug_name("review_queue_next")) && queue.position + 1 < queue.commits.size()) {
            ++queue.position;
            select();
        }
        if (button(ctx, mk(row.ent(), 3), preset::Button("Reviewed and next")
                .with_size(ComponentSize{children(), pixels(28)}).with_font_size(pixels(12)).with_debug_name("review_queue_complete"))) {
            if (cache.patch.key.commit != repo.selectedCommitHash() || cache.patch.key.parent != selected_commit_parent(repo) || cache.patch_loading() ||
                !cache.commitDetailError.empty() ||
                current_review_verdict(review, commit_review_scope(repo), cache.commitDetailDiff) == ReviewVerdict::InProgress)
                afterhours::toast::send_info(ctx, "Finish this commit's review before completing it", 2.f);
            else {
                queue.completed.insert(repo.selectedCommitHash());
                if (queue.position + 1 < queue.commits.size()) ++queue.position;
                select();
            }
        }
    }
    if (button(ctx, mk(row.ent(), 4), preset::Button("Exit queue")
            .with_size(ComponentSize{children(), pixels(28)}).with_font_size(pixels(12)).with_debug_name("review_queue_exit"))) close_review_queue(repo);
}

}
