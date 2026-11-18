#include "test_framework.h"
#include "../../src/util/async_slot.h"
#include "../../src/ecs/components.h"

#include <future>
#include <stdexcept>
#include <string>

namespace {

struct FakeWorkspace {
    reading::Location loc = reading::ReviewLocation{};
    std::uint64_t gen = 0;
    const reading::Location& location() const { return loc; }
    std::uint64_t generation() const { return gen; }
};

struct FakeRepo {
    std::string repoPath = "/tmp/repo";
    unsigned dataGeneration = 7;
    FakeWorkspace ws;
    const FakeWorkspace& workspace() const { return ws; }
};

reading::RequestStamp stamp_for(const FakeRepo& repo, std::string key) {
    return {repo.repoPath, repo.ws.location(), std::move(key), repo.ws.generation(), repo.dataGeneration};
}

}  // namespace

TEST(async_slot_name_covers_all_phases) {
    ASSERT_STREQ(std::string(async_slot::name(async_slot::Phase::Idle)), "Idle");
    ASSERT_STREQ(std::string(async_slot::name(async_slot::Phase::Pending)), "Pending");
    ASSERT_STREQ(std::string(async_slot::name(async_slot::Phase::Ready)), "Ready");
    ASSERT_STREQ(std::string(async_slot::name(async_slot::Phase::Failed)), "Failed");
}

TEST(async_slot_request_then_poll_ready) {
    FakeRepo repo;
    async_slot::Slot<int> slot;
    ASSERT_EQ(slot.phase, async_slot::Phase::Idle);
    ASSERT_FALSE(slot.busy());
    slot.request("key-1", stamp_for(repo, "key-1"), async_work::completed(42));
    ASSERT_EQ(slot.phase, async_slot::Phase::Pending);
    ASSERT_TRUE(slot.busy());
    ASSERT_STREQ(slot.key, "key-1");
    auto value = slot.poll(repo);
    ASSERT_TRUE(value.has_value());
    ASSERT_EQ(*value, 42);
    ASSERT_EQ(slot.phase, async_slot::Phase::Ready);
    ASSERT_FALSE(slot.busy());
    ASSERT_TRUE(slot.error.empty());
    // A consumed result is not handed out twice.
    ASSERT_FALSE(slot.poll(repo).has_value());
    ASSERT_EQ(slot.phase, async_slot::Phase::Ready);
}

TEST(async_slot_poll_while_pending_returns_nothing) {
    FakeRepo repo;
    async_slot::Slot<int> slot;
    std::promise<int> pending;
    std::stop_source stop;
    slot.request("k", stamp_for(repo, "k"),
                 async_work::Task<int>(pending.get_future(), stop));
    ASSERT_TRUE(slot.busy());
    ASSERT_FALSE(slot.poll(repo).has_value());
    ASSERT_EQ(slot.phase, async_slot::Phase::Pending);
    pending.set_value(1);
    auto value = slot.poll(repo);
    ASSERT_TRUE(value.has_value());
    ASSERT_EQ(*value, 1);
    ASSERT_EQ(slot.phase, async_slot::Phase::Ready);
}

TEST(async_slot_cancel_goes_idle) {
    FakeRepo repo;
    async_slot::Slot<int> slot;
    std::promise<int> pending;
    std::stop_source stop;
    slot.request("k", stamp_for(repo, "k"),
                 async_work::Task<int>(pending.get_future(), stop));
    ASSERT_TRUE(slot.busy());
    slot.cancel();
    ASSERT_EQ(slot.phase, async_slot::Phase::Idle);
    ASSERT_FALSE(slot.busy());
    ASSERT_FALSE(slot.task.valid());
    ASSERT_TRUE(stop.stop_requested());
    ASSERT_FALSE(slot.poll(repo).has_value());
    ASSERT_EQ(slot.phase, async_slot::Phase::Idle);
    pending.set_value(9);
}

TEST(async_slot_stale_stamp_rejected) {
    FakeRepo repo;
    async_slot::Slot<int> slot;
    // Stamp issued for an older workspace generation.
    auto stale = stamp_for(repo, "k");
    stale.generation = repo.ws.generation() + 1;
    slot.request("k", stale, async_work::completed(42));
    ASSERT_TRUE(slot.busy());
    ASSERT_FALSE(slot.poll(repo).has_value());
    ASSERT_EQ(slot.phase, async_slot::Phase::Idle);
    ASSERT_FALSE(slot.busy());
    ASSERT_FALSE(slot.task.valid());

    // Repository mismatch is stale too.
    async_slot::Slot<int> other;
    auto wrongRepo = stamp_for(repo, "k");
    wrongRepo.repository = "/tmp/other";
    other.request("k", wrongRepo, async_work::completed(1));
    ASSERT_FALSE(other.poll(repo).has_value());
    ASSERT_EQ(other.phase, async_slot::Phase::Idle);

    // Data-generation mismatch is stale too.
    async_slot::Slot<int> third;
    auto wrongData = stamp_for(repo, "k");
    wrongData.dataGeneration = repo.dataGeneration + 1;
    third.request("k", wrongData, async_work::completed(2));
    ASSERT_FALSE(third.poll(repo).has_value());
    ASSERT_EQ(third.phase, async_slot::Phase::Idle);
}

TEST(async_slot_failed_task_retains_error) {
    FakeRepo repo;
    async_slot::Slot<int> slot;
    std::promise<int> failed;
    failed.set_exception(std::make_exception_ptr(std::runtime_error("boom failure")));
    slot.request("k", stamp_for(repo, "k"),
                 async_work::Task<int>(failed.get_future(), std::stop_source{}));
    ASSERT_FALSE(slot.poll(repo).has_value());
    ASSERT_EQ(slot.phase, async_slot::Phase::Failed);
    ASSERT_FALSE(slot.busy());
    ASSERT_STREQ(slot.error, "boom failure");
    // Error is retained until the next request / cancel / invalidate.
    ASSERT_FALSE(slot.poll(repo).has_value());
    ASSERT_STREQ(slot.error, "boom failure");
    slot.request("k2", stamp_for(repo, "k2"), async_work::completed(5));
    ASSERT_TRUE(slot.error.empty());
    ASSERT_EQ(slot.phase, async_slot::Phase::Pending);
    auto value = slot.poll(repo);
    ASSERT_TRUE(value.has_value());
    ASSERT_EQ(*value, 5);
}

TEST(async_slot_invalidate_clears_key_stamp_and_error) {
    FakeRepo repo;
    async_slot::Slot<std::string> slot;
    std::promise<std::string> pending;
    slot.request("abc", stamp_for(repo, "abc"),
                 async_work::Task<std::string>(pending.get_future(), std::stop_source{}));
    ASSERT_TRUE(slot.busy());
    slot.invalidate();
    ASSERT_EQ(slot.phase, async_slot::Phase::Idle);
    ASSERT_FALSE(slot.busy());
    ASSERT_STREQ(slot.key, "");
    ASSERT_STREQ(slot.stamp.repository, "");
    ASSERT_TRUE(slot.error.empty());
    ASSERT_FALSE(slot.task.valid());
    pending.set_value("late");
}

TEST(repo_async_slot_registry_names_are_unique_and_findable) {
    ecs::RepoComponent repo;
    auto slots = repo.async_slots();
    ASSERT_FALSE(slots.empty());
    for (size_t i = 0; i < slots.size(); ++i) {
        ASSERT_FALSE(slots[i].name.empty());
        ASSERT_TRUE(repo.find_async_slot(slots[i].name) != nullptr);
        for (size_t j = i + 1; j < slots.size(); ++j)
            ASSERT_TRUE(slots[i].name != slots[j].name);
    }
    ASSERT_TRUE(repo.find_async_slot("no_such_slot") == nullptr);
    ASSERT_FALSE(repo.async_slot_phase("no_such_slot").has_value());
    // Every registered name resolves to a phase name.
    for (const auto& slot : slots) {
        auto phase = repo.async_slot_phase(slot.name);
        ASSERT_TRUE(phase.has_value());
        ASSERT_FALSE(phase->empty());
    }
}

TEST(repo_async_slot_registry_idle_repo_is_idle) {
    ecs::RepoComponent repo;
    ASSERT_TRUE(repo.async_idle());
    auto phase = repo.async_slot_phase("file_history");
    ASSERT_TRUE(phase.has_value());
    ASSERT_STREQ(std::string(*phase), "Idle");
    const auto* slot = repo.find_async_slot("file_history");
    ASSERT_TRUE(slot != nullptr);
    ASSERT_TRUE(slot->gates_idle);
    ASSERT_FALSE(slot->busy(repo));
}

namespace {

reading::RequestStamp repo_stamp(const ecs::RepoComponent& repo, std::string key) {
    return {repo.repoPath, repo.workspace().location(), key, repo.workspace().generation(), repo.dataGeneration};
}

}  // namespace

TEST(repo_async_slot_registry_pending_task_is_busy_and_pending) {
    ecs::RepoComponent repo;
    std::promise<git::GitResult> pending;
    repo.fileHistory.request("k", repo_stamp(repo, "k"),
        async_work::Task<git::GitResult>(pending.get_future(), std::stop_source{}));
    const auto* slot = repo.find_async_slot("file_history");
    ASSERT_TRUE(slot != nullptr);
    ASSERT_TRUE(slot->busy(repo));
    ASSERT_FALSE(repo.async_idle());
    auto phase = repo.async_slot_phase("file_history");
    ASSERT_TRUE(phase.has_value());
    ASSERT_STREQ(std::string(*phase), "Pending");
    pending.set_value(git::GitResult{});
}

TEST(repo_async_slot_registry_ready_task_is_not_busy) {
    ecs::RepoComponent repo;
    repo.fileHistory.request("k", repo_stamp(repo, "k"), async_work::completed(git::GitResult{}));
    // A completed-but-unpolled slot is still Pending; poll consumes it.
    ASSERT_TRUE(repo.fileHistory.busy());
    auto value = repo.fileHistory.poll(repo);
    ASSERT_TRUE(value.has_value());
    const auto* slot = repo.find_async_slot("file_history");
    ASSERT_TRUE(slot != nullptr);
    ASSERT_FALSE(slot->busy(repo));
    ASSERT_TRUE(repo.async_idle());
    auto phase = repo.async_slot_phase("file_history");
    ASSERT_TRUE(phase.has_value());
    ASSERT_STREQ(std::string(*phase), "Ready");
}

TEST(migrated_slots_poll_to_ready_and_name_phases) {
    ecs::RepoComponent repo;
    repo.commitSearch.request("search-key", repo_stamp(repo, "search-key"), async_work::completed(git::GitResult{}));
    ASSERT_STREQ(std::string(*repo.async_slot_phase("commit_search")), "Pending");
    ASSERT_TRUE(repo.commitSearch.poll(repo).has_value());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("commit_search")), "Ready");
    ASSERT_FALSE(repo.commitSearch.busy());

    repo.reviewQueue.request("queue:a:b", repo_stamp(repo, "queue:a:b"), async_work::completed(git::GitResult{}));
    ASSERT_STREQ(repo.reviewQueue.key, "queue:a:b");
    ASSERT_STREQ(std::string(*repo.async_slot_phase("review_queue")), "Pending");
    ASSERT_TRUE(repo.reviewQueue.poll(repo).has_value());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("review_queue")), "Ready");
    repo.reviewQueue.invalidate();
    ASSERT_STREQ(std::string(*repo.async_slot_phase("review_queue")), "Idle");
    ASSERT_TRUE(repo.reviewQueue.key.empty());
}

TEST(migrated_selection_copy_and_markdown_link_slots_poll_and_name_phases) {
    ecs::RepoComponent repo;
    repo.repoPath = "/tmp/repo";
    repo.selectionCopy.slot.request("selection-copy", repo_stamp(repo, "selection-copy"),
        async_work::completed(ecs::SelectionCopyResult{.text = "hi"}));
    ASSERT_STREQ(std::string(*repo.async_slot_phase("selection_copy")), "Ready");
    ASSERT_TRUE(repo.selectionCopy.slot.poll(repo).has_value());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("selection_copy")), "Ready");
    ASSERT_FALSE(repo.selectionCopy.slot.busy());
    repo.markdownLink.slot.request("a.md#Intro", repo_stamp(repo, "a.md#Intro"),
        async_work::completed(ecs::OutlineResult{}));
    ASSERT_TRUE(repo.markdownLink.slot.poll(repo).has_value());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("markdown_link")), "Ready");
    repo.markdownLink.slot.invalidate();
    ASSERT_STREQ(std::string(*repo.async_slot_phase("markdown_link")), "Idle");
}

TEST(migrated_file_history_slot_rejects_stale_stamp) {
    ecs::RepoComponent repo;
    auto stale = repo_stamp(repo, "k");
    stale.generation = repo.workspace().generation() + 1;
    repo.fileHistory.request("k", stale, async_work::completed(git::GitResult{}));
    ASSERT_TRUE(repo.fileHistory.busy());
    ASSERT_FALSE(repo.fileHistory.poll(repo).has_value());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("file_history")), "Idle");
    ASSERT_FALSE(repo.fileHistory.busy());
    ASSERT_TRUE(repo.async_idle());
}

TEST(repo_async_slot_registry_valid_only_machines_stay_busy_until_consumed) {
    // range_diff / push_destination gated on future.valid() alone before the
    // registry; a ready-but-unconsumed result must still hold the gate.
    // range_diff now reports its Step (M7): Resolving is busy until poll.
    ecs::RepoComponent repo;
    repo.rangeDiff.begin_resolving(repo_stamp(repo, "range"), {"a", "b", "c", "d"},
                                   async_work::completed(git::GitResult{}));
    const auto* slot = repo.find_async_slot("range_diff");
    ASSERT_TRUE(slot != nullptr);
    ASSERT_TRUE(slot->gates_idle);
    ASSERT_TRUE(slot->busy(repo));
    ASSERT_FALSE(repo.async_idle());
    auto phase = repo.async_slot_phase("range_diff");
    ASSERT_TRUE(phase.has_value());
    ASSERT_STREQ(std::string(*phase), "Resolving");
    repo.rangeDiff.abandon();
    ASSERT_TRUE(repo.async_idle());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("range_diff")), "Idle");
}

TEST(repo_async_slot_registry_non_gating_slots_do_not_hold_idle) {
    // selected_diff / blame_gutter were not in the wait_for_refresh gate;
    // registering them must not change gate behaviour.
    ecs::RepoComponent repo;
    std::promise<git::GitResult> pending;
    repo.selectedDiff.begin("k", async_work::Task<git::GitResult>(pending.get_future(), std::stop_source{}));
    const auto* slot = repo.find_async_slot("selected_diff");
    ASSERT_TRUE(slot != nullptr);
    ASSERT_FALSE(slot->gates_idle);
    ASSERT_TRUE(slot->busy(repo));
    ASSERT_TRUE(repo.async_idle());
    auto phase = repo.async_slot_phase("selected_diff");
    ASSERT_TRUE(phase.has_value());
    ASSERT_STREQ(std::string(*phase), "SinglePending");
    pending.set_value(git::GitResult{});
}

TEST(semantic_registry_names_lsp_phases) {
    ecs::RepoComponent repo;
    const auto* slot = repo.find_async_slot("semantic");
    ASSERT_TRUE(slot != nullptr);
    ASSERT_FALSE(slot->gates_idle);
    ASSERT_FALSE(slot->busy(repo));
    ASSERT_TRUE(repo.async_idle());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("semantic")), "Unstarted");
    ASSERT_FALSE(slot->busy(repo));
}

TEST(diff_source_name_covers_all_values) {
    ASSERT_STREQ(std::string(ecs::diff_source_name(ecs::DiffSource::Whole)), "Whole");
    ASSERT_STREQ(std::string(ecs::diff_source_name(ecs::DiffSource::SinglePending)), "SinglePending");
    ASSERT_STREQ(std::string(ecs::diff_source_name(ecs::DiffSource::Single)), "Single");
    ASSERT_STREQ(std::string(ecs::diff_source_name(ecs::DiffSource::Failed)), "Failed");
}

TEST(selected_diff_begin_land_and_use_whole) {
    ecs::RepoComponent repo;
    ASSERT_EQ(repo.selectedDiff.source, ecs::DiffSource::Whole);
    ASSERT_STREQ(std::string(*repo.async_slot_phase("selected_diff")), "Whole");
    std::promise<git::GitResult> pending;
    repo.selectedDiff.begin("key-1", async_work::Task<git::GitResult>(pending.get_future(), std::stop_source{}));
    ASSERT_TRUE(repo.selectedDiff.pending());
    ASSERT_FALSE(repo.selectedDiff.ready());
    pending.set_value(git::GitResult{});
    ASSERT_TRUE(repo.selectedDiff.ready());
    auto result = repo.selectedDiff.take();
    (void)result;
    ecs::FileDiff diff;
    diff.filePath = "a.cpp";
    repo.selectedDiff.land(diff);
    ASSERT_EQ(repo.selectedDiff.source, ecs::DiffSource::Single);
    ASSERT_TRUE(repo.selectedDiff.diff.has_value());
    ASSERT_STREQ(repo.selectedDiff.diff->filePath, "a.cpp");
    ASSERT_STREQ(std::string(*repo.async_slot_phase("selected_diff")), "Single");
    // The whole-repo diff landing takes over and drops the single read.
    repo.selectedDiff.use_whole();
    ASSERT_EQ(repo.selectedDiff.source, ecs::DiffSource::Whole);
    ASSERT_FALSE(repo.selectedDiff.diff.has_value());
    ASSERT_TRUE(repo.selectedDiff.key.empty());
    ASSERT_FALSE(repo.selectedDiff.task.valid());
}

TEST(selected_diff_failure_is_failed_with_error_not_previous_diff) {
    ecs::SelectedDiffRead read;
    ecs::FileDiff previous;
    previous.filePath = "old.cpp";
    read.land(previous);
    ASSERT_TRUE(read.diff.has_value());
    read.begin("key-2", async_work::completed(git::GitResult{}));
    // Beginning a new read drops the previous file's diff at once.
    ASSERT_FALSE(read.diff.has_value());
    ASSERT_TRUE(read.pending());
    read.fail("fatal: bad revision");
    ASSERT_TRUE(read.failed());
    ASSERT_EQ(read.source, ecs::DiffSource::Failed);
    ASSERT_STREQ(read.error, "fatal: bad revision");
    ASSERT_FALSE(read.diff.has_value());
    ASSERT_FALSE(read.task.valid());
    // A new key retries from Failed.
    read.begin("key-3", async_work::completed(git::GitResult{}));
    ASSERT_TRUE(read.pending());
    ASSERT_TRUE(read.error.empty());
}

TEST(untracked_review_phase_name_covers_all_values) {
    ASSERT_STREQ(std::string(ecs::untracked_review_phase_name(ecs::UntrackedReviewPhase::Stale)), "Stale");
    ASSERT_STREQ(std::string(ecs::untracked_review_phase_name(ecs::UntrackedReviewPhase::Loading)), "Loading");
    ASSERT_STREQ(std::string(ecs::untracked_review_phase_name(ecs::UntrackedReviewPhase::Ready)), "Ready");
}

TEST(untracked_review_read_lifecycle_and_staleness) {
    ecs::RepoComponent repo;
    repo.repoPath = "/tmp/repo";
    repo.dataGeneration = 4;
    repo.untrackedFiles = {"new.cpp"};
    ASSERT_EQ(repo.untrackedReview.phase, ecs::UntrackedReviewPhase::Stale);
    ASSERT_TRUE(repo.untrackedReview.needs_read(repo.dataGeneration, repo.repoPath));
    ASSERT_STREQ(std::string(*repo.async_slot_phase("untracked_review")), "Stale");
    const auto* slot = repo.find_async_slot("untracked_review");
    ASSERT_TRUE(slot != nullptr);
    ASSERT_TRUE(slot->busy(repo));
    ASSERT_FALSE(repo.async_idle());

    ecs::UntrackedReviewFiles landed;
    ecs::FileDiff preview;
    preview.filePath = "new.cpp";
    landed.files.push_back(preview);
    landed.notice = "note";
    repo.untrackedReview.begin(repo.dataGeneration, repo.repoPath, async_work::completed(landed));
    ASSERT_TRUE(repo.untrackedReview.busy());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("untracked_review")), "Loading");
    auto result = repo.untrackedReview.poll();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->files.size(), 1u);
    ASSERT_EQ(repo.untrackedReview.phase, ecs::UntrackedReviewPhase::Ready);
    ASSERT_STREQ(repo.untrackedReview.notice, "note");
    ASSERT_TRUE(repo.untrackedReview.current_for(repo.dataGeneration, repo.repoPath));
    ASSERT_FALSE(slot->busy(repo));
    ASSERT_TRUE(repo.async_idle());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("untracked_review")), "Ready");

    // A data-generation bump makes the landed read stale without a writer.
    ++repo.dataGeneration;
    ASSERT_TRUE(repo.untrackedReview.needs_read(repo.dataGeneration, repo.repoPath));
    ASSERT_TRUE(slot->busy(repo));
    // Invalidating (landed whole-repo diff) drops back to Stale.
    repo.untrackedReview.invalidate();
    ASSERT_EQ(repo.untrackedReview.phase, ecs::UntrackedReviewPhase::Stale);
    ASSERT_FALSE(repo.untrackedReview.task.valid());
    // Settling with no files to read records the generation as covered.
    repo.untrackedFiles.clear();
    repo.untrackedReview.settle(repo.dataGeneration, repo.repoPath);
    ASSERT_TRUE(repo.untrackedReview.current_for(repo.dataGeneration, repo.repoPath));
    ASSERT_FALSE(slot->busy(repo));
}

TEST(untracked_review_stale_in_flight_is_invalidated) {
    ecs::UntrackedReviewRead read;
    std::promise<ecs::UntrackedReviewFiles> pending;
    read.begin(3, "/tmp/repo", async_work::Task<ecs::UntrackedReviewFiles>(pending.get_future(), std::stop_source{}));
    ASSERT_FALSE(read.stale_in_flight(3, "/tmp/repo"));
    ASSERT_TRUE(read.stale_in_flight(4, "/tmp/repo"));
    ASSERT_TRUE(read.stale_in_flight(3, "/tmp/other"));
    read.invalidate();
    ASSERT_EQ(read.phase, ecs::UntrackedReviewPhase::Stale);
    ASSERT_FALSE(read.task.valid());
    pending.set_value(ecs::UntrackedReviewFiles{});
}

TEST(repo_async_slot_registry_refresh_flag_is_busy) {
    ecs::RepoComponent repo;
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::NeverLoaded);
    ASSERT_STREQ(std::string(*repo.async_slot_phase("refresh")), "NeverLoaded");
    ASSERT_TRUE(repo.async_idle());
    repo.request_refresh();
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::Queued);
    ASSERT_FALSE(repo.async_idle());
    auto phase = repo.async_slot_phase("refresh");
    ASSERT_TRUE(phase.has_value());
    ASSERT_STREQ(std::string(*phase), "Queued");
    auto scope = repo.begin_refresh();
    ASSERT_TRUE(scope == refresh_scope::Scope::Full);
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::Running);
    ASSERT_FALSE(repo.async_idle());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("refresh")), "Running");
    repo.finish_refresh();
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::Settled);
    ASSERT_TRUE(repo.async_idle());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("refresh")), "Settled");
}

TEST(refresh_phase_name_covers_all_phases) {
    ASSERT_STREQ(std::string(ecs::refresh_phase_name(ecs::RefreshPhase::NeverLoaded)), "NeverLoaded");
    ASSERT_STREQ(std::string(ecs::refresh_phase_name(ecs::RefreshPhase::Queued)), "Queued");
    ASSERT_STREQ(std::string(ecs::refresh_phase_name(ecs::RefreshPhase::Running)), "Running");
    ASSERT_STREQ(std::string(ecs::refresh_phase_name(ecs::RefreshPhase::Settled)), "Settled");
}

TEST(refresh_request_begin_finish_cycle_and_loaded_latch) {
    ecs::RepoComponent repo;
    ASSERT_FALSE(repo.has_loaded_once());
    ASSERT_FALSE(repo.refresh_busy());
    repo.request_refresh(refresh_scope::Scope::Worktree);
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::Queued);
    ASSERT_TRUE(repo.refresh_requested());
    // The initial queued scope is Full, so a scoped first request stays Full.
    ASSERT_TRUE(repo.begin_refresh() == refresh_scope::Scope::Full);
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::Running);
    ASSERT_TRUE(repo.refreshing());
    ASSERT_FALSE(repo.has_loaded_once());
    repo.finish_refresh();
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::Settled);
    ASSERT_TRUE(repo.has_loaded_once());
    // A re-refresh keeps the loaded latch through Queued and Running.
    repo.request_refresh(refresh_scope::Scope::Refs);
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::Queued);
    ASSERT_TRUE(repo.has_loaded_once());
    ASSERT_TRUE(repo.begin_refresh() == refresh_scope::Scope::Refs);
    ASSERT_TRUE(repo.has_loaded_once());
    repo.finish_refresh();
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::Settled);
}

TEST(refresh_request_while_running_queues_next_run) {
    ecs::RepoComponent repo;
    repo.request_refresh();
    ASSERT_TRUE(repo.begin_refresh() == refresh_scope::Scope::Full);
    repo.request_refresh(refresh_scope::Scope::Index);
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::Running);
    repo.finish_refresh();
    // The mid-run request is not lost: it lands Queued with its scope.
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::Queued);
    ASSERT_TRUE(repo.has_loaded_once());
    ASSERT_TRUE(repo.begin_refresh() == refresh_scope::Scope::Index);
    repo.finish_refresh();
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::Settled);
    // A plain request while Running queues a Full run.
    repo.request_refresh();
    ASSERT_TRUE(repo.begin_refresh() == refresh_scope::Scope::Full);
    repo.request_refresh();
    repo.finish_refresh();
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::Queued);
    ASSERT_TRUE(repo.begin_refresh() == refresh_scope::Scope::Full);
    repo.finish_refresh();
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::Settled);
}

TEST(refresh_requeue_and_abandon_restore_idle_phase) {
    ecs::RepoComponent repo;
    repo.request_refresh();
    (void)repo.begin_refresh();
    repo.requeue_refresh();
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::Queued);
    (void)repo.begin_refresh();
    repo.abandon_refresh();
    // Never settled: abandoning returns to NeverLoaded.
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::NeverLoaded);
    ASSERT_FALSE(repo.has_loaded_once());
    repo.settle_refresh();
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::Settled);
    repo.request_refresh();
    (void)repo.begin_refresh();
    repo.abandon_refresh();
    ASSERT_EQ(repo.refreshPhase, ecs::RefreshPhase::Settled);
    ASSERT_TRUE(repo.has_loaded_once());
}

TEST(async_slot_unstamped_request_then_poll_unstamped) {
    async_slot::Slot<int> slot;
    slot.request("Push", async_work::completed(7));
    ASSERT_EQ(slot.phase, async_slot::Phase::Pending);
    ASSERT_TRUE(slot.ready());
    auto value = slot.poll_unstamped();
    ASSERT_TRUE(value.has_value());
    ASSERT_EQ(*value, 7);
    ASSERT_EQ(slot.phase, async_slot::Phase::Ready);

    async_slot::Slot<int> failed;
    std::promise<int> boom;
    boom.set_exception(std::make_exception_ptr(std::runtime_error("net boom")));
    failed.request("Fetch", async_work::Task<int>(boom.get_future(), std::stop_source{}));
    ASSERT_FALSE(failed.poll_unstamped().has_value());
    ASSERT_EQ(failed.phase, async_slot::Phase::Failed);
    ASSERT_STREQ(failed.error, "net boom");
}

TEST(network_op_pending_phase_elapsed_and_cancel) {
    ecs::PendingNetworkOp op;
    op.label = "Push";
    op.repository = "/tmp/repo";
    std::promise<git::GitResult> pending;
    std::stop_source stop;
    op.slot.request("Push", async_work::Task<git::GitResult>(pending.get_future(), stop));
    op.startedAt = std::chrono::steady_clock::now() - std::chrono::seconds(3);
    ASSERT_TRUE(op.busy());
    ASSERT_EQ(op.phase(), async_slot::Phase::Pending);
    ASSERT_STREQ(std::string(async_slot::name(op.phase())), "Pending");
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        op.elapsed(op.startedAt + std::chrono::seconds(5))).count();
    ASSERT_EQ(elapsed, 5);
    // Cancel requests stop via Task::cancel(); the op stays Pending
    // until the polling system consumes the cancelled result.
    op.cancel();
    ASSERT_TRUE(stop.stop_requested());
    ASSERT_TRUE(op.busy());
    ASSERT_EQ(op.phase(), async_slot::Phase::Pending);
    pending.set_value(git::GitResult{});
    auto done = op.slot.poll_unstamped();
    ASSERT_TRUE(done.has_value());
    ASSERT_EQ(op.phase(), async_slot::Phase::Ready);
    ASSERT_FALSE(op.busy());
}

TEST(network_ops_component_aggregate_phase_names_pending) {
    ecs::NetworkOpsComponent ops;
    ASSERT_FALSE(ops.busy());
    ASSERT_STREQ(std::string(async_slot::name(ops.phase())), "Idle");
    ecs::PendingNetworkOp op;
    std::promise<git::GitResult> pending;
    op.slot.request("Fetch", async_work::Task<git::GitResult>(pending.get_future(), std::stop_source{}));
    ops.pending.push_back(std::move(op));
    ASSERT_TRUE(ops.busy());
    ASSERT_STREQ(std::string(async_slot::name(ops.phase())), "Pending");
    ops.pending.clear();
    ASSERT_STREQ(std::string(async_slot::name(ops.phase())), "Idle");
    pending.set_value(git::GitResult{});
}

TEST(log_phase_name_covers_all_phases) {
    ASSERT_STREQ(std::string(ecs::log_phase_name(ecs::LogPhase::Idle)), "Idle");
    ASSERT_STREQ(std::string(ecs::log_phase_name(ecs::LogPhase::LoadingScope)), "LoadingScope");
    ASSERT_STREQ(std::string(ecs::log_phase_name(ecs::LogPhase::LoadingPage)), "LoadingPage");
    ASSERT_STREQ(std::string(ecs::log_phase_name(ecs::LogPhase::Error)), "Error");
}

TEST(commit_log_has_more_computed_from_returned_vs_requested) {
    ASSERT_EQ(git::kCommitLogPageSize, static_cast<size_t>(100));
    ASSERT_TRUE(ecs::commit_log_has_more(100, 100));
    ASSERT_TRUE(ecs::commit_log_has_more(100));
    ASSERT_FALSE(ecs::commit_log_has_more(99, 100));
    ASSERT_FALSE(ecs::commit_log_has_more(0, 100));
    ASSERT_TRUE(ecs::commit_log_has_more(3, 3));
    ASSERT_FALSE(ecs::commit_log_has_more(2, 3));
}

TEST(commit_log_page_phase_transitions_and_has_more) {
    ecs::CommitLogPage page;
    ASSERT_EQ(page.phase, ecs::LogPhase::Idle);
    ASSERT_TRUE(page.hasMore);
    ASSERT_FALSE(page.busy());
    page.begin_scope();
    ASSERT_EQ(page.phase, ecs::LogPhase::LoadingScope);
    ASSERT_TRUE(page.busy());
    page.finish(100);
    ASSERT_EQ(page.phase, ecs::LogPhase::Idle);
    ASSERT_TRUE(page.hasMore);
    page.request_page();
    ASSERT_EQ(page.phase, ecs::LogPhase::LoadingPage);
    ASSERT_TRUE(page.busy());
    // A second request while busy is a no-op.
    page.request_page();
    ASSERT_EQ(page.phase, ecs::LogPhase::LoadingPage);
    page.finish(37);
    ASSERT_EQ(page.phase, ecs::LogPhase::Idle);
    ASSERT_FALSE(page.hasMore);
    page.request_page();
    page.fail("Unable to load older commits");
    ASSERT_EQ(page.phase, ecs::LogPhase::Error);
    ASSERT_FALSE(page.busy());
    ASSERT_STREQ(page.error, "Unable to load older commits");
    // Retry from Error clears the error and re-enters LoadingPage.
    page.request_page();
    ASSERT_EQ(page.phase, ecs::LogPhase::LoadingPage);
    ASSERT_TRUE(page.error.empty());
}

TEST(repo_commit_log_page_registry_names_log_phases) {
    ecs::RepoComponent repo;
    ASSERT_STREQ(std::string(*repo.async_slot_phase("commit_log_page")), "Idle");
    ASSERT_STREQ(std::string(*repo.async_slot_phase("history_scope")), "Idle");
    repo.commitLogPage.begin_scope();
    ASSERT_TRUE(repo.commitLogPage.busy());
    ASSERT_FALSE(repo.async_idle());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("commit_log_page")), "LoadingScope");
    ASSERT_STREQ(std::string(*repo.async_slot_phase("history_scope")), "LoadingScope");
    repo.commitLogPage.finish(100);
    ASSERT_TRUE(repo.async_idle());
    ASSERT_TRUE(repo.commitLogPage.hasMore);
    repo.commitLogPage.request_page();
    ASSERT_FALSE(repo.async_idle());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("commit_log_page")), "LoadingPage");
    repo.commitLogPage.fail("boom");
    ASSERT_TRUE(repo.async_idle());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("commit_log_page")), "Error");
    ASSERT_STREQ(repo.commitLogPage.error, "boom");
}

TEST(patch_key_struct_equality_covers_all_five_fields) {
    const ecs::PatchKey base{"/repo", "abc", "def", 3, false};
    ASSERT_TRUE(base == ecs::PatchKey({"/repo", "abc", "def", 3, false}));
    ASSERT_FALSE(base == ecs::PatchKey({"/other", "abc", "def", 3, false}));
    ASSERT_FALSE(base == ecs::PatchKey({"/repo", "xyz", "def", 3, false}));
    ASSERT_FALSE(base == ecs::PatchKey({"/repo", "abc", "xyz", 3, false}));
    ASSERT_FALSE(base == ecs::PatchKey({"/repo", "abc", "def", 0, false}));
    ASSERT_FALSE(base == ecs::PatchKey({"/repo", "abc", "def", 3, true}));
}

TEST(commit_patch_slot_polls_to_ready_with_patch_key) {
    ecs::RepoComponent repo;
    ecs::CommitDetailCache cache;
    const ecs::PatchKey key{repo.repoPath, "abc", "", repo.diffContext, repo.ignoreWhitespace};
    ecs::CommitPatch patch;
    patch.metadata = "subject";
    cache.patch.request(key, repo_stamp(repo, "k"), async_work::completed(patch));
    ASSERT_EQ(cache.patch.phase, async_slot::Phase::Pending);
    ASSERT_TRUE(cache.patch.busy());
    ASSERT_TRUE(cache.patch_loading());
    ASSERT_TRUE(cache.patch.key == key);
    auto value = cache.patch.poll(repo);
    ASSERT_TRUE(value.has_value());
    ASSERT_STREQ(value->metadata, "subject");
    ASSERT_EQ(cache.patch.phase, async_slot::Phase::Ready);
    ASSERT_FALSE(cache.patch_loading());
    ASSERT_TRUE(cache.patch.key == key);
}

TEST(commit_patch_slot_revalidating_is_not_a_cold_load) {
    ecs::RepoComponent repo;
    ecs::CommitDetailCache cache;
    std::promise<ecs::CommitPatch> pending;
    cache.patch.request({"r", "c", "", 3, false}, repo_stamp(repo, "k"),
        async_work::Task<ecs::CommitPatch>(pending.get_future(), std::stop_source{}));
    cache.patchRevalidating = true;
    ASSERT_TRUE(cache.patch.busy());
    ASSERT_FALSE(cache.patch_loading());
    cache.patchRevalidating = false;
    ASSERT_TRUE(cache.patch_loading());
    pending.set_value(ecs::CommitPatch{});
}

TEST(commit_patch_slot_stale_stamp_drops_to_idle) {
    ecs::RepoComponent repo;
    ecs::CommitDetailCache cache;
    auto stale = repo_stamp(repo, "k");
    stale.generation = repo.workspace().generation() + 1;
    cache.patch.request({"r", "c", "", 3, false}, stale, async_work::completed(ecs::CommitPatch{}));
    ASSERT_TRUE(cache.patch.busy());
    ASSERT_FALSE(cache.patch.poll(repo).has_value());
    ASSERT_EQ(cache.patch.phase, async_slot::Phase::Idle);
    ASSERT_FALSE(cache.patch_loading());
}

TEST(commit_flow_name_covers_all_phases) {
    ASSERT_STREQ(std::string(ecs::commit_flow_name(ecs::CommitFlow::Editing)), "Editing");
    ASSERT_STREQ(std::string(ecs::commit_flow_name(ecs::CommitFlow::AwaitingPolicy)), "AwaitingPolicy");
    ASSERT_STREQ(std::string(ecs::commit_flow_name(ecs::CommitFlow::Committing)), "Committing");
    ASSERT_STREQ(std::string(ecs::commit_flow_name(ecs::CommitFlow::Done)), "Done");
}

TEST(commit_editor_request_commit_is_pending_until_launched) {
    ecs::CommitEditorComponent editor;
    ASSERT_EQ(editor.flow, ecs::CommitFlow::Editing);
    ASSERT_FALSE(editor.commit_requested());
    // The split-button one-shot override is set before request_commit()
    // and must survive it untouched (consumed only by the handler).
    editor.commitPolicyOverride = ecs::CommitEditorComponent::UnstagedPolicy::StageAll;
    editor.request_commit();
    ASSERT_EQ(editor.flow, ecs::CommitFlow::Committing);
    ASSERT_TRUE(editor.commit_requested());
    ASSERT_TRUE(editor.commitPolicyOverride.has_value());
    ASSERT_TRUE(*editor.commitPolicyOverride == ecs::CommitEditorComponent::UnstagedPolicy::StageAll);
    // Re-requesting while pending is a no-op (flag-set idempotence).
    editor.request_commit();
    ASSERT_EQ(editor.flow, ecs::CommitFlow::Committing);
    // Launching the commit keeps Committing but is no longer pending.
    std::promise<ecs::CommitEditorComponent::CommitOutcome> pending;
    editor.commitFuture = async_work::Task<ecs::CommitEditorComponent::CommitOutcome>(
        pending.get_future(), std::stop_source{});
    editor.begin_committing();
    ASSERT_EQ(editor.flow, ecs::CommitFlow::Committing);
    ASSERT_FALSE(editor.commit_requested());
    pending.set_value(ecs::CommitEditorComponent::CommitOutcome{});
}

TEST(commit_editor_policy_dialog_and_done_transitions) {
    ecs::CommitEditorComponent editor;
    editor.rememberChoice = true;
    editor.request_commit();
    editor.await_policy();
    ASSERT_EQ(editor.flow, ecs::CommitFlow::AwaitingPolicy);
    ASSERT_FALSE(editor.rememberChoice);
    ASSERT_FALSE(editor.commit_requested());
    // request_commit() does not reopen/close an open policy dialog.
    editor.request_commit();
    ASSERT_EQ(editor.flow, ecs::CommitFlow::AwaitingPolicy);
    editor.dismiss_policy();
    ASSERT_EQ(editor.flow, ecs::CommitFlow::Editing);
    // Success lands in Done; the next request leaves Done for Committing;
    // failure returns to Editing.
    editor.request_commit();
    editor.finish_commit();
    ASSERT_EQ(editor.flow, ecs::CommitFlow::Done);
    ASSERT_FALSE(editor.commit_requested());
    editor.request_commit();
    ASSERT_EQ(editor.flow, ecs::CommitFlow::Committing);
    ASSERT_TRUE(editor.commit_requested());
    editor.abort_commit();
    ASSERT_EQ(editor.flow, ecs::CommitFlow::Editing);
}

TEST(commit_editor_cancel_request_never_cancels_in_flight_commit) {
    ecs::CommitEditorComponent pendingEditor;
    pendingEditor.request_commit();
    pendingEditor.cancel_request();
    ASSERT_EQ(pendingEditor.flow, ecs::CommitFlow::Editing);

    ecs::CommitEditorComponent dialogEditor;
    dialogEditor.request_commit();
    dialogEditor.await_policy();
    dialogEditor.cancel_request();
    ASSERT_EQ(dialogEditor.flow, ecs::CommitFlow::Editing);

    ecs::CommitEditorComponent committingEditor;
    std::promise<ecs::CommitEditorComponent::CommitOutcome> pending;
    committingEditor.commitFuture = async_work::Task<ecs::CommitEditorComponent::CommitOutcome>(
        pending.get_future(), std::stop_source{});
    committingEditor.begin_committing();
    committingEditor.cancel_request();
    ASSERT_EQ(committingEditor.flow, ecs::CommitFlow::Committing);
    ASSERT_TRUE(committingEditor.commitFuture.valid());
    pending.set_value(ecs::CommitEditorComponent::CommitOutcome{});
}

TEST(picker_phase_and_mode_names_cover_all_values) {
    ASSERT_STREQ(std::string(ecs::picker_phase_name(ecs::PickerPhase::Closed)), "Closed");
    ASSERT_STREQ(std::string(ecs::picker_phase_name(ecs::PickerPhase::Browsing)), "Browsing");
    ASSERT_STREQ(std::string(ecs::picker_phase_name(ecs::PickerPhase::Matching)), "Matching");
    ASSERT_STREQ(std::string(ecs::picker_phase_name(ecs::PickerPhase::Previewing)), "Previewing");
    ASSERT_STREQ(std::string(ecs::picker_phase_name(ecs::PickerPhase::Confirming)), "Confirming");
    ASSERT_STREQ(std::string(ecs::picker_mode_name(ecs::PickerMode::Files)), "Files");
    ASSERT_STREQ(std::string(ecs::picker_mode_name(ecs::PickerMode::Line)), "Line");
    ASSERT_STREQ(std::string(ecs::picker_mode_name(ecs::PickerMode::Symbol)), "Symbol");
    ASSERT_STREQ(std::string(ecs::picker_mode_name(ecs::PickerMode::ProjectSymbol)), "ProjectSymbol");
    ASSERT_STREQ(std::string(ecs::picker_mode_name(ecs::PickerMode::Commands)), "Commands");
    ASSERT_STREQ(std::string(ecs::picker_mode_name(ecs::PickerMode::Repositories)), "Repositories");
}

TEST(repo_picker_opens_and_closes_on_shared_picker_shell) {
    ecs::LayoutComponent layout;
    ASSERT_FALSE(layout.repo_picker_open());
    ASSERT_EQ(layout.picker.phase, ecs::PickerPhase::Closed);
    layout.repoPicker.index = 4;
    layout.open_repo_picker_shell();
    ASSERT_TRUE(layout.repo_picker_open());
    ASSERT_TRUE(layout.picker.is_open());
    ASSERT_EQ(layout.picker.phase, ecs::PickerPhase::Browsing);
    ASSERT_EQ(layout.picker.mode, ecs::PickerMode::Repositories);
    ASSERT_TRUE(layout.repoPicker.focusPending);
    ASSERT_EQ(layout.repoPicker.index, 0);
    // The repo shell does not borrow the file picker's focus flag.
    ASSERT_FALSE(layout.filePickerFocus);
    layout.confirm_picker();
    ASSERT_FALSE(layout.repo_picker_open());
    ASSERT_EQ(layout.picker.phase, ecs::PickerPhase::Closed);
    layout.open_repo_picker_shell();
    ASSERT_TRUE(layout.repo_picker_open());
    layout.cancel_picker();
    ASSERT_FALSE(layout.repo_picker_open());
    ASSERT_EQ(layout.picker.phase, ecs::PickerPhase::Closed);
}

TEST(repo_picker_scope_shares_navigation_between_surfaces) {
    ecs::RepoPickerScope scope;
    ASSERT_TRUE(scope.focusPending);
    ASSERT_FALSE(scope.alphabetical);
    ASSERT_FALSE(scope.resolving());
    scope.index = 9;
    scope.apply_keys(3, false, false);
    ASSERT_EQ(scope.index, 2);
    scope.apply_keys(3, true, false);
    ASSERT_EQ(scope.index, 2);
    scope.apply_keys(3, false, true);
    ASSERT_EQ(scope.index, 1);
    scope.index = 0;
    scope.apply_keys(3, false, true);
    ASSERT_EQ(scope.index, 0);
    scope.apply_keys(0, true, false);
    ASSERT_EQ(scope.index, 0);
    scope.query = "shared-filter";
    ASSERT_STREQ(scope.query, "shared-filter");
}

TEST(picker_open_saves_restore_and_browses_in_one_mode) {
    ecs::LayoutComponent layout;
    ASSERT_EQ(layout.picker.phase, ecs::PickerPhase::Closed);
    ASSERT_FALSE(layout.picker.is_open());
    ecs::RepoComponent repo;
    layout.open_picker(ecs::PickerMode::Files, repo);
    ASSERT_EQ(layout.picker.phase, ecs::PickerPhase::Browsing);
    ASSERT_EQ(layout.picker.mode, ecs::PickerMode::Files);
    ASSERT_TRUE(layout.picker.is_open());
    ASSERT_TRUE(layout.filePickerFocus);
    ASSERT_TRUE(layout.picker.restoreLocation.has_value());
    // One mode field: opening another mode replaces, never combines.
    layout.open_picker(ecs::PickerMode::Symbol, repo);
    ASSERT_EQ(layout.picker.mode, ecs::PickerMode::Symbol);
    ASSERT_EQ(layout.picker.phase, ecs::PickerPhase::Browsing);
    ASSERT_TRUE(layout.picker.previewKey.empty());
}

TEST(picker_matching_previewing_and_confirming_transitions) {
    ecs::Picker picker;
    picker.open(ecs::PickerMode::Files, reading::Location{reading::ReviewLocation{}}, std::nullopt);
    ASSERT_EQ(picker.phase, ecs::PickerPhase::Browsing);
    picker.begin_matching();
    ASSERT_EQ(picker.phase, ecs::PickerPhase::Matching);
    // Previous results stay: finishing with no preview returns to Browsing.
    picker.finish_matching();
    ASSERT_EQ(picker.phase, ecs::PickerPhase::Browsing);
    picker.begin_preview("src/a.cpp");
    ASSERT_EQ(picker.phase, ecs::PickerPhase::Previewing);
    ASSERT_TRUE(picker.previewing());
    // A new match while previewing finishes back in Previewing.
    picker.begin_matching();
    picker.finish_matching();
    ASSERT_EQ(picker.phase, ecs::PickerPhase::Previewing);
    // Enter before the match lands is Confirming, with its Keep payload.
    picker.request_confirm(false);
    ASSERT_EQ(picker.phase, ecs::PickerPhase::Confirming);
    ASSERT_FALSE(picker.confirmKeep);
    picker.consume_confirm();
    ASSERT_EQ(picker.phase, ecs::PickerPhase::Previewing);
}

TEST(picker_confirm_discards_restore_cancel_keeps_it) {
    ecs::Picker confirmed;
    confirmed.open(ecs::PickerMode::Line, reading::Location{reading::ReviewLocation{}}, reading::ReadingAnchor{});
    confirmed.begin_preview("line:3:1");
    confirmed.confirm();
    ASSERT_EQ(confirmed.phase, ecs::PickerPhase::Closed);
    ASSERT_FALSE(confirmed.restoreLocation.has_value());
    ASSERT_TRUE(confirmed.previewKey.empty());

    ecs::Picker cancelled;
    cancelled.open(ecs::PickerMode::Symbol, reading::Location{reading::ReviewLocation{}}, reading::ReadingAnchor{});
    cancelled.begin_preview("symbol:9");
    cancelled.cancel();
    ASSERT_EQ(cancelled.phase, ecs::PickerPhase::Closed);
    // Kept for the picker system's exact restore, then reset.
    ASSERT_TRUE(cancelled.restoreLocation.has_value());
    ASSERT_STREQ(cancelled.previewKey, "symbol:9");
    cancelled.reset();
    ASSERT_FALSE(cancelled.restoreLocation.has_value());
    ASSERT_TRUE(cancelled.previewKey.empty());
}

TEST(picker_match_stamp_replaces_search_id_pair) {
    ecs::FilePickerScope scope;
    ASSERT_TRUE(scope.matchStamp.key.empty());
    scope.matchStamp = {"/tmp/repo", reading::Location{reading::ReviewLocation{}}, "match-key", 3, 7};
    ASSERT_STREQ(scope.matchStamp.key, "match-key");
    ASSERT_EQ(scope.matchStamp.generation, std::uint64_t{3});
    ASSERT_EQ(scope.matchStamp.dataGeneration, 7u);
}

TEST(modal_popup_names_cover_all_values) {
    using reading::focus::Popup;
    for (auto popup : {Popup::Menu, Popup::ContextMenu, Popup::Picker, Popup::Find, Popup::Search,
                       Popup::SearchPreview, Popup::Feedback, Popup::CommitSearch, Popup::FileHistory,
                       Popup::Composer, Popup::Options, Popup::Snapshot, Popup::ComparisonEditor,
                       Popup::CommitDetails, Popup::PushDialog, Popup::RelinkDialog, Popup::Shortcuts,
                       Popup::NewBranch, Popup::DeleteBranch, Popup::ForceDeleteBranch, Popup::Stash})
        ASSERT_FALSE(reading::focus::popup_name(popup).empty());
    ASSERT_STREQ(std::string(reading::focus::popup_name(Popup::PushDialog)), "PushDialog");
    ASSERT_STREQ(std::string(reading::focus::popup_name(Popup::ForceDeleteBranch)), "ForceDeleteBranch");
}

TEST(modal_open_close_top_and_single_open_dialog) {
    using reading::focus::Popup;
    ecs::LayoutComponent layout;
    ASSERT_FALSE(layout.modal.has_value());
    ASSERT_STREQ(std::string(layout.modal_name()), "None");
    layout.open_modal(Popup::PushDialog);
    ASSERT_TRUE(layout.modal_is(Popup::PushDialog));
    ASSERT_STREQ(std::string(layout.modal_name()), "PushDialog");
    // Opening another dialog replaces the one stored modal: two dialogs
    // can no longer be open at once with precedence by read order.
    layout.open_modal(Popup::Shortcuts);
    ASSERT_FALSE(layout.modal_is(Popup::PushDialog));
    ASSERT_TRUE(layout.modal_is(Popup::Shortcuts));
    layout.close_modal(Popup::PushDialog);  // closing a non-open dialog is a no-op
    ASSERT_TRUE(layout.modal_is(Popup::Shortcuts));
    layout.close_top_modal();
    ASSERT_FALSE(layout.modal.has_value());
}

TEST(modal_dialog_payloads_hold_data_without_open_bools) {
    ecs::RepoComponent repo;
    repo.pushDialog.repository = "/tmp/repo";
    repo.pushDialog.destination.remote = "origin";
    ASSERT_EQ(repo.pushDialog.repository, std::string("/tmp/repo"));
    ASSERT_EQ(repo.pushDialog.destination.remote, std::string("origin"));
    ecs::BranchDialogState branch;
    branch.newBranchName = "topic";
    branch.deleteBranchName = "old";
    branch.stashMessage = "wip";
    ASSERT_EQ(branch.newBranchName, std::string("topic"));
    ecs::LayoutComponent layout;
    layout.relinkDialog.oldPath = "/old";
    layout.relinkDialog.newPath = "/new";
    ASSERT_EQ(layout.relinkDialog.oldPath, std::string("/old"));
    // Comparison editor open is the stored Comparison phase (M7), not
    // derived from the modal or the workspace scope.
    ASSERT_FALSE(repo.comparisonOpen());
    repo.open_comparison_editor();
    ASSERT_TRUE(repo.comparisonOpen());
    ASSERT_EQ(repo.comparison, ecs::Comparison::Editing);
    repo.close_comparison();
    ASSERT_FALSE(repo.comparisonOpen());
}

TEST(find_name_covers_all_phases) {
    ASSERT_STREQ(std::string(reading::find_name(reading::Find::Closed)), "Closed");
    ASSERT_STREQ(std::string(reading::find_name(reading::Find::Editing)), "Editing");
    ASSERT_STREQ(std::string(reading::find_name(reading::Find::Scanning)), "Scanning");
}

TEST(find_state_phase_transitions_preserve_pending_step_data) {
    reading::FindState find;
    ASSERT_EQ(find.phase, reading::Find::Closed);
    ASSERT_FALSE(find.is_open());
    find.open();
    ASSERT_EQ(find.phase, reading::Find::Editing);
    ASSERT_TRUE(find.is_open());
    ASSERT_TRUE(find.focus);
    find.pendingStep = 3;
    find.sync_scanning(true);
    ASSERT_EQ(find.phase, reading::Find::Scanning);
    ASSERT_TRUE(find.scanning());
    // pendingStep is data accumulated while scanning, not a phase.
    ASSERT_EQ(find.pendingStep, 3);
    find.sync_scanning(false);
    ASSERT_EQ(find.phase, reading::Find::Editing);
    ASSERT_EQ(find.pendingStep, 3);
    find.query = "alpha";
    find.index = 2;
    find.close();
    ASSERT_EQ(find.phase, reading::Find::Closed);
    ASSERT_EQ(find.pendingStep, 0);
    ASSERT_STREQ(find.query, "alpha");
    ASSERT_EQ(find.index, 2u);
    // A closed find does not resurrect into Scanning.
    find.sync_scanning(true);
    ASSERT_EQ(find.phase, reading::Find::Closed);
}

TEST(source_find_slot_polls_to_ready_and_names_registry_phases) {
    ecs::RepoComponent repo;
    ecs::SourceFindResult result;
    result.matches.push_back({3, 5});
    repo.sourceFind.slot.request("find-key", repo_stamp(repo, "find-key"), async_work::completed(result));
    ASSERT_TRUE(repo.sourceFind.busy());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("source_find")), "Pending");
    ASSERT_FALSE(repo.async_idle());
    auto landed = repo.sourceFind.slot.poll(repo);
    ASSERT_TRUE(landed.has_value());
    ASSERT_EQ(landed->matches.size(), 1u);
    if (landed) repo.sourceFind.result = *landed;
    ASSERT_EQ(repo.sourceFind.result.matches[0].line, 3);
    ASSERT_STREQ(std::string(*repo.async_slot_phase("source_find")), "Ready");
    ASSERT_TRUE(repo.async_idle());
    repo.sourceFind.cancel_scan();
    ASSERT_EQ(repo.sourceFind.slot.phase, async_slot::Phase::Idle);
    ASSERT_STREQ(std::string(*repo.async_slot_phase("source_find")), "Idle");
    // cancel_scan retains key + landed result (reopen reuses them).
    ASSERT_STREQ(repo.sourceFind.slot.key, "find-key");
    ASSERT_EQ(repo.sourceFind.result.matches.size(), 1u);
    repo.sourceFind.clear();
    ASSERT_TRUE(repo.sourceFind.slot.key.empty());
    ASSERT_TRUE(repo.sourceFind.result.matches.empty());
}

TEST(source_find_slot_stale_stamp_drops_to_idle) {
    ecs::RepoComponent repo;
    auto stale = repo_stamp(repo, "k");
    stale.generation = repo.workspace().generation() + 1;
    repo.sourceFind.slot.request("k", stale, async_work::completed(ecs::SourceFindResult{}));
    ASSERT_TRUE(repo.sourceFind.busy());
    ASSERT_FALSE(repo.sourceFind.slot.poll(repo).has_value());
    ASSERT_EQ(repo.sourceFind.slot.phase, async_slot::Phase::Idle);
    ASSERT_TRUE(repo.async_idle());
}

TEST(file_load_name_covers_all_phases) {
    ASSERT_STREQ(std::string(ecs::file_load_name(ecs::FileLoad::Idle)), "Idle");
    ASSERT_STREQ(std::string(ecs::file_load_name(ecs::FileLoad::Loading)), "Loading");
    ASSERT_STREQ(std::string(ecs::file_load_name(ecs::FileLoad::Revalidating)), "Revalidating");
    ASSERT_STREQ(std::string(ecs::file_load_name(ecs::FileLoad::Extending)), "Extending");
    ASSERT_STREQ(std::string(ecs::file_load_name(ecs::FileLoad::Ready)), "Ready");
    ASSERT_STREQ(std::string(ecs::file_load_name(ecs::FileLoad::Failed)), "Failed");
}

TEST(page_key_equality_covers_source_encoding_action_cursor_target) {
    ecs::FilePageRequest request;
    request.action = ecs::FilePageRequest::Action::Next;
    request.cursor.offset = 4096;
    request.cursor.line = 120;
    request.targetLine = 42;
    request.leadingLines = 3;
    request.targetColumn = 7;
    const auto base = ecs::make_page_key(
        ecs::make_file_source("/tmp/repo", "abc123", "src/a.cpp", 9), "utf8", request);
    // Committed revision: the data generation is not part of the source.
    auto same = base;
    ASSERT_TRUE(base == same);
    ASSERT_EQ(ecs::make_file_source("/tmp/repo", "abc123", "src/a.cpp", 99).dataGeneration, 0u);
    auto bySource = base; bySource.source.path = "src/b.cpp"; ASSERT_FALSE(base == bySource);
    auto byRevision = base; byRevision.source.revision = "def456"; ASSERT_FALSE(base == byRevision);
    auto byRepo = base; byRepo.source.repo = "/tmp/other"; ASSERT_FALSE(base == byRepo);
    auto byEncoding = base; byEncoding.encoding = "auto"; ASSERT_FALSE(base == byEncoding);
    auto byAction = base; byAction.action = ecs::FilePageRequest::Action::Previous; ASSERT_FALSE(base == byAction);
    auto byCursor = base; byCursor.cursor.offset = 8192; ASSERT_FALSE(base == byCursor);
    auto byTarget = base; byTarget.target.line = 43; ASSERT_FALSE(base == byTarget);
    auto byLeading = base; byLeading.target.leadingLines = 4; ASSERT_FALSE(base == byLeading);
    auto byColumn = base; byColumn.target.column = 8; ASSERT_FALSE(base == byColumn);
    // Working-tree reads do carry the data generation in the source.
    const auto wt = ecs::make_file_source("/tmp/repo", "INDEX", "src/a.cpp", 9);
    ASSERT_EQ(wt.dataGeneration, 9u);
    ASSERT_EQ(ecs::make_file_source("/tmp/repo", "", "src/a.cpp", 9).dataGeneration, 9u);
    auto wtKey = ecs::make_page_key(wt, "auto", request);
    wtKey.source.dataGeneration = 10;
    ASSERT_FALSE(ecs::make_page_key(wt, "auto", request) == wtKey);
}

TEST(full_file_load_lifecycle_peek_revalidate_extend_and_fail) {
    ecs::RepoComponent repo;
    repo.repoPath = "/tmp/repo";
    auto& load = repo.fullFileLoad;
    ASSERT_EQ(load.phase, ecs::FileLoad::Idle);
    ASSERT_STREQ(std::string(*repo.async_slot_phase("full_file")), "Idle");
    ecs::FilePageRequest start;
    const auto key = ecs::make_page_key(ecs::make_file_source("/tmp/repo", "HEAD", "a.cpp", 0), "auto", start);
    ASSERT_TRUE(load.needs_load(key));
    // Cold load: Loading until the page lands, then Ready.
    load.begin(key, repo_stamp(repo, ""), async_work::completed(ecs::FullFileContent{}),
               async_work::Task<ecs::FullFileContent>{}, false);
    ASSERT_EQ(load.phase, ecs::FileLoad::Loading);
    ASSERT_TRUE(load.busy());
    ASSERT_FALSE(load.needs_load(key));
    ASSERT_TRUE(load.task.valid());
    (void)load.task.get();
    load.land();
    ASSERT_EQ(load.phase, ecs::FileLoad::Ready);
    ASSERT_STREQ(std::string(*repo.async_slot_phase("full_file")), "Ready");
    // Peek: page lands with a revalidate behind it -> Revalidating, then
    // the agreeing revalidate settles Ready.
    std::promise<ecs::FullFileContent> validating;
    load.begin(key, repo_stamp(repo, ""), async_work::completed(ecs::FullFileContent{}),
               async_work::Task<ecs::FullFileContent>(validating.get_future(), std::stop_source{}), false);
    (void)load.task.get();
    load.land();
    ASSERT_EQ(load.phase, ecs::FileLoad::Revalidating);
    ASSERT_TRUE(load.revalidating());
    ASSERT_FALSE(load.busy());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("full_file")), "Revalidating");
    validating.set_value(ecs::FullFileContent{});
    ASSERT_TRUE(load.revalidate.valid());
    (void)load.revalidate.get();
    load.settle_revalidation();
    ASSERT_EQ(load.phase, ecs::FileLoad::Ready);
    // Extend: old page stays, phase is Extending (not Loading).
    ecs::FilePageRequest next;
    next.action = ecs::FilePageRequest::Action::Next;
    next.cursor.offset = 4096;
    const auto nextKey = ecs::make_page_key(key.source, "auto", next);
    load.begin(nextKey, repo_stamp(repo, ""), async_work::completed(ecs::FullFileContent{}),
               async_work::Task<ecs::FullFileContent>{}, true);
    ASSERT_EQ(load.phase, ecs::FileLoad::Extending);
    ASSERT_TRUE(load.extending());
    ASSERT_TRUE(load.busy());
    (void)load.task.get();
    load.fail("Source changed while scrolling. Reload file to continue.");
    ASSERT_EQ(load.phase, ecs::FileLoad::Failed);
    ASSERT_TRUE(load.failed());
    ASSERT_STREQ(load.error, "Source changed while scrolling. Reload file to continue.");
    ASSERT_STREQ(std::string(*repo.async_slot_phase("full_file")), "Failed");
    // Requesting a page and resetting return to a loadable Idle.
    load.request_page({});
    ASSERT_TRUE(load.needs_load(key));
    load.reset();
    ASSERT_EQ(load.phase, ecs::FileLoad::Idle);
    ASSERT_FALSE(load.key.has_value());
    ASSERT_FALSE(load.source.has_value());
    ASSERT_TRUE(load.error.empty());
}

TEST(hunk_context_scope_and_diff_syntax_slots_cache_failures) {
    ecs::RepoComponent repo;
    // Hunk context: key carries identity + renderIdentity-derived version.
    ecs::HunkContextResult failed;
    failed.error = "Context changed; refresh this review before expanding";
    repo.hunkContext.slot.request({"a.cpp\nabove", "1:7:10:3"}, repo_stamp(repo, "1:7:10:3"),
        async_work::completed(failed));
    ASSERT_EQ(repo.hunkContext.slot.phase, async_slot::Phase::Pending);
    // A completed-but-unpolled task reads Ready in the registry, as the
    // raw-task phase did before the migration.
    ASSERT_STREQ(std::string(*repo.async_slot_phase("hunk_context")), "Ready");
    auto landed = repo.hunkContext.slot.poll(repo);
    ASSERT_TRUE(landed.has_value());
    repo.hunkContext.entries[repo.hunkContext.slot.key.identity] =
        {repo.hunkContext.slot.key.version, *landed};
    repo.hunkContext.slot.fail(landed->error);
    ASSERT_STREQ(std::string(*repo.async_slot_phase("hunk_context")), "Failed");
    ASSERT_EQ(repo.hunkContext.entries.at("a.cpp\nabove").result.error,
              std::string("Context changed; refresh this review before expanding"));
    ASSERT_FALSE(repo.hunkContext.slot.busy());
    // A new request supersedes the cached failure.
    repo.hunkContext.slot.request({"a.cpp\nabove", "1:7:10:3"}, repo_stamp(repo, "1:7:10:3"),
        async_work::completed(ecs::HunkContextResult{}));
    ASSERT_TRUE(repo.hunkContext.slot.poll(repo).has_value());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("hunk_context")), "Ready");

    // Hunk scopes: a failed key is cached, not relaunched.
    repo.hunkScopes.slot.request("wt\na.cpp\nHEAD\n0", repo_stamp(repo, "wt\na.cpp\nHEAD\n0"),
        async_work::completed(ecs::OutlineResult{{}, "Binary files have no symbols"}));
    auto outline = repo.hunkScopes.slot.poll(repo);
    ASSERT_TRUE(outline.has_value());
    repo.hunkScopes.failed.insert(repo.hunkScopes.slot.key);
    repo.hunkScopes.slot.fail(outline->error);
    ASSERT_STREQ(std::string(*repo.async_slot_phase("hunk_scopes")), "Failed");
    ASSERT_TRUE(repo.hunkScopes.failed.contains("wt\na.cpp\nHEAD\n0"));

    // Diff syntax: slot key is the file renderIdentity.
    ecs::DiffSyntaxResult syntaxFailed;
    syntaxFailed.error = "Syntax reader is busy";
    repo.diffSyntax["wt"].slot.request(42, repo_stamp(repo, "syntax:42"),
        async_work::completed(syntaxFailed));
    ASSERT_EQ(repo.diffSyntax["wt"].slot.key, 42u);
    auto syntax = repo.diffSyntax["wt"].slot.poll(repo);
    ASSERT_TRUE(syntax.has_value());
    repo.diffSyntax["wt"].slot.fail(syntax->error);
    ASSERT_STREQ(std::string(*repo.async_slot_phase("diff_syntax")), "Failed");
}

TEST(blame_slots_poll_fail_and_name_phases) {
    ecs::RepoComponent repo;
    git::GitResult ok;
    // Line blame success -> Ready, rendered line parsed by the caller.
    repo.blame.request("12", repo_stamp(repo, "12"), async_work::completed(ok));
    ASSERT_EQ(repo.blame.phase, async_slot::Phase::Pending);
    ASSERT_STREQ(std::string(*repo.async_slot_phase("blame")), "Ready");
    ASSERT_TRUE(repo.blame.poll(repo).has_value());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("blame")), "Ready");
    // Gutter blame failure -> Failed with the stderr retained; the slot
    // is non-gating, so it does not hold the idle gate.
    git::GitResult bad;
    repo.blameGutter.request("repo\nfile\nHEAD\n0", repo_stamp(repo, "repo\nfile\nHEAD\n0"),
        async_work::completed(bad));
    auto gutter = repo.blameGutter.poll(repo);
    ASSERT_TRUE(gutter.has_value());
    repo.blameGutter.fail("fatal: no such path");
    ASSERT_STREQ(std::string(*repo.async_slot_phase("blame_gutter")), "Failed");
    ASSERT_STREQ(repo.blameGutter.error, "fatal: no such path");
    const auto* slot = repo.find_async_slot("blame_gutter");
    ASSERT_TRUE(slot != nullptr);
    ASSERT_FALSE(slot->gates_idle);
    ASSERT_TRUE(repo.async_idle());
    // A stale gutter stamp drops to Idle instead of landing.
    auto stale = repo_stamp(repo, "k");
    stale.generation = repo.workspace().generation() + 1;
    repo.blameGutter.request("k", stale, async_work::completed(git::GitResult{}));
    ASSERT_FALSE(repo.blameGutter.poll(repo).has_value());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("blame_gutter")), "Idle");
}

TEST(comparison_name_covers_all_phases) {
    ASSERT_STREQ(std::string(ecs::comparison_name(ecs::Comparison::Closed)), "Closed");
    ASSERT_STREQ(std::string(ecs::comparison_name(ecs::Comparison::Editing)), "Editing");
    ASSERT_STREQ(std::string(ecs::comparison_name(ecs::Comparison::Loading)), "Loading");
    ASSERT_STREQ(std::string(ecs::comparison_name(ecs::Comparison::Loaded)), "Loaded");
    ASSERT_STREQ(std::string(ecs::comparison_name(ecs::Comparison::Failed)), "Failed");
}

TEST(comparison_stored_phase_transitions_and_names_registry) {
    ecs::RepoComponent repo;
    ASSERT_EQ(repo.comparison, ecs::Comparison::Closed);
    ASSERT_FALSE(repo.comparisonOpen());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("comparison")), "Closed");
    repo.open_comparison_editor();
    ASSERT_EQ(repo.comparison, ecs::Comparison::Editing);
    ASSERT_TRUE(repo.comparisonOpen());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("comparison")), "Editing");
    repo.begin_comparison_load();
    ASSERT_STREQ(std::string(*repo.async_slot_phase("comparison")), "Loading");
    repo.finish_comparison_load();
    ASSERT_STREQ(std::string(*repo.async_slot_phase("comparison")), "Loaded");
    ASSERT_TRUE(repo.comparisonOpen());
    repo.fail_comparison("no common ancestor");
    ASSERT_EQ(repo.comparison, ecs::Comparison::Failed);
    ASSERT_STREQ(repo.comparisonError, "no common ancestor");
    ASSERT_STREQ(std::string(*repo.async_slot_phase("comparison")), "Failed");
    // Cancelling a load that never left the panel returns to the form.
    repo.begin_comparison_load();
    repo.cancel_comparison_load();
    ASSERT_EQ(repo.comparison, ecs::Comparison::Editing);
    ASSERT_FALSE(repo.comparisonFuture.valid());
    repo.close_comparison();
    ASSERT_FALSE(repo.comparisonOpen());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("comparison")), "Closed");
}

TEST(range_diff_step_names_and_lifecycle) {
    ASSERT_STREQ(std::string(ecs::range_diff_step_name(ecs::RangeDiff::Step::Idle)), "Idle");
    ASSERT_STREQ(std::string(ecs::range_diff_step_name(ecs::RangeDiff::Step::Resolving)), "Resolving");
    ASSERT_STREQ(std::string(ecs::range_diff_step_name(ecs::RangeDiff::Step::Comparing)), "Comparing");
    ASSERT_STREQ(std::string(ecs::range_diff_step_name(ecs::RangeDiff::Step::Done)), "Done");
    ASSERT_STREQ(std::string(ecs::range_diff_step_name(ecs::RangeDiff::Step::Failed)), "Failed");

    ecs::RepoComponent repo;
    auto& state = repo.rangeDiff;
    ASSERT_EQ(state.step, ecs::RangeDiff::Step::Idle);
    ASSERT_FALSE(state.busy());
    state.begin_resolving(repo_stamp(repo, "range"), {"a", "b", "c", "d"},
                          async_work::completed(git::GitResult{}));
    ASSERT_EQ(state.step, ecs::RangeDiff::Step::Resolving);
    ASSERT_TRUE(state.busy());
    state.begin_comparing(async_work::completed(git::GitResult{}));
    ASSERT_EQ(state.step, ecs::RangeDiff::Step::Comparing);
    ASSERT_STREQ(std::string(*repo.async_slot_phase("range_diff")), "Comparing");
    state.finish({ecs::FileDiff{}});
    ASSERT_EQ(state.step, ecs::RangeDiff::Step::Done);
    ASSERT_FALSE(state.busy());
    ASSERT_EQ(state.display.size(), 1u);
    ASSERT_STREQ(std::string(*repo.async_slot_phase("range_diff")), "Done");
    state.begin();
    ASSERT_TRUE(state.display.empty());
    ASSERT_TRUE(state.error.empty());
    state.fail("bad range");
    ASSERT_EQ(state.step, ecs::RangeDiff::Step::Failed);
    ASSERT_STREQ(state.error, "bad range");
    ASSERT_STREQ(std::string(*repo.async_slot_phase("range_diff")), "Failed");
}

TEST(range_diff_abandoned_resolution_clears_resolved_for_next_open) {
    ecs::RepoComponent repo;
    auto& state = repo.rangeDiff;
    state.begin_resolving(repo_stamp(repo, "range"), {"a", "b", "c", "d"},
                          async_work::completed(git::GitResult{}));
    // A resolution that filled only some endpoints before being left.
    state.resolved[0] = std::string(40, 'a');
    state.resolved[1] = std::string(40, 'b');
    state.abandon();
    ASSERT_EQ(state.step, ecs::RangeDiff::Step::Idle);
    ASSERT_FALSE(state.future.valid());
    for (const auto& hash : state.resolved) ASSERT_TRUE(hash.empty());
    ASSERT_TRUE(repo.async_idle());
    // A completed comparison is not an abandoned resolution: Done and
    // its resolved endpoints survive a mode toggle (abandon is a no-op).
    state.begin_resolving(repo_stamp(repo, "range"), {"a", "b", "c", "d"},
                          async_work::completed(git::GitResult{}));
    state.resolved = {std::string(40, 'a'), std::string(40, 'b'), std::string(40, 'c'), std::string(40, 'd')};
    state.begin_comparing(async_work::completed(git::GitResult{}));
    state.finish({ecs::FileDiff{}});
    state.abandon();
    ASSERT_EQ(state.step, ecs::RangeDiff::Step::Done);
    ASSERT_EQ(state.resolved[0], std::string(40, 'a'));
    // Leaving the document resets the whole machine.
    state.reset();
    ASSERT_EQ(state.step, ecs::RangeDiff::Step::Idle);
    ASSERT_TRUE(state.display.empty());
    for (const auto& hash : state.resolved) ASSERT_TRUE(hash.empty());
}

TEST(async_slot_fail_lands_failed_with_error_and_key) {
    FakeRepo repo;
    async_slot::Slot<int> slot;
    slot.request("k", stamp_for(repo, "k"), async_work::completed(7));
    ASSERT_TRUE(slot.poll(repo).has_value());
    slot.fail("reader failed");
    ASSERT_EQ(slot.phase, async_slot::Phase::Failed);
    ASSERT_STREQ(slot.error, "reader failed");
    ASSERT_STREQ(slot.key, "k");
    ASSERT_FALSE(slot.busy());
    ASSERT_FALSE(slot.task.valid());
}

TEST(search_name_covers_all_phases) {
    ASSERT_STREQ(std::string(ecs::search_name(ecs::Search::Closed)), "Closed");
    ASSERT_STREQ(std::string(ecs::search_name(ecs::Search::Debouncing)), "Debouncing");
    ASSERT_STREQ(std::string(ecs::search_name(ecs::Search::Running)), "Running");
    ASSERT_STREQ(std::string(ecs::search_name(ecs::Search::Done)), "Done");
    ASSERT_STREQ(std::string(ecs::search_name(ecs::Search::Failed)), "Failed");
    ASSERT_STREQ(std::string(ecs::search_name(ecs::Search::External)), "External");
}

TEST(search_phase_transitions_and_registry_names) {
    ecs::RepoComponent repo;
    ASSERT_EQ(repo.repoSearchPhase, ecs::Search::Closed);
    ASSERT_FALSE(repo.repo_search_open());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("repo_search")), "Closed");
    ASSERT_TRUE(repo.async_idle());
    repo.open_search();
    ASSERT_EQ(repo.repoSearchPhase, ecs::Search::Done);
    ASSERT_TRUE(repo.repo_search_open());
    // Typing debounces, then running, then Done (no matches is Done).
    repo.repoSearchQuery = "needle";
    ASSERT_TRUE(repo.search_text_changed());
    repo.debounce_search(std::chrono::steady_clock::now());
    ASSERT_EQ(repo.repoSearchPhase, ecs::Search::Debouncing);
    ASSERT_FALSE(repo.search_text_changed());
    ASSERT_FALSE(repo.async_idle());
    repo.begin_search();
    ASSERT_EQ(repo.repoSearchPhase, ecs::Search::Running);
    ASSERT_FALSE(repo.async_idle());
    repo.finish_search();
    ASSERT_EQ(repo.repoSearchPhase, ecs::Search::Done);
    ASSERT_TRUE(repo.async_idle());
    ASSERT_STREQ(std::string(*repo.async_slot_phase("repo_search")), "Done");
    // Failure is distinct from Done/no-matches and retains the error.
    repo.begin_search();
    repo.repoSearchError = "grep failed";
    repo.finish_search();
    ASSERT_EQ(repo.repoSearchPhase, ecs::Search::Failed);
    ASSERT_STREQ(std::string(*repo.async_slot_phase("repo_search")), "Failed");
    repo.begin_search();
    repo.fail_search("bad regex");
    ASSERT_EQ(repo.repoSearchPhase, ecs::Search::Failed);
    ASSERT_STREQ(repo.repoSearchError, "bad regex");
    repo.close_search();
    ASSERT_EQ(repo.repoSearchPhase, ecs::Search::Closed);
    ASSERT_FALSE(repo.repo_search_open());
}

TEST(search_external_results_do_not_look_like_a_text_edit) {
    ecs::RepoComponent repo;
    repo.open_search();
    repo.repoSearchQuery = "symbol";
    repo.repoSearchIncludeGlob = "src/**";
    repo.show_search_external("3 references of symbol · clangd");
    ASSERT_EQ(repo.repoSearchPhase, ecs::Search::External);
    ASSERT_STREQ(std::string(*repo.async_slot_phase("repo_search")), "External");
    ASSERT_TRUE(repo.async_idle());
    // The injected text is the submitted text: no debounce, no re-search.
    ASSERT_FALSE(repo.search_text_changed());
    // Editing the query or a glob leaves External for a text search.
    repo.repoSearchQuery = "other";
    ASSERT_TRUE(repo.search_text_changed());
    repo.debounce_search(std::chrono::steady_clock::now());
    ASSERT_EQ(repo.repoSearchPhase, ecs::Search::Debouncing);
    repo.repoSearchQuery = "symbol";
    repo.begin_search();
    repo.show_search_external("3 references of symbol · clangd");
    repo.repoSearchExcludeGlob = "**/test*";
    ASSERT_TRUE(repo.search_text_changed());
    // Closing and reopening with external results held returns to External.
    repo.repoSearchExcludeGlob.clear();
    repo.show_search_external("3 references of symbol · clangd");
    repo.close_search();
    repo.open_search();
    ASSERT_EQ(repo.repoSearchPhase, ecs::Search::External);
}

TEST(edit_name_covers_all_phases) {
    ASSERT_STREQ(std::string(reading::edit_name(reading::Edit::Viewing)), "Viewing");
    ASSERT_STREQ(std::string(reading::edit_name(reading::Edit::Seeding)), "Seeding");
    ASSERT_STREQ(std::string(reading::edit_name(reading::Edit::EditingClean)), "EditingClean");
    ASSERT_STREQ(std::string(reading::edit_name(reading::Edit::EditingDirty)), "EditingDirty");
    ASSERT_STREQ(std::string(reading::edit_name(reading::Edit::Saving)), "Saving");
    ASSERT_STREQ(std::string(reading::edit_name(reading::Edit::SaveFailed)), "SaveFailed");
}

TEST(edit_phase_transitions_and_dirty_derivation) {
    reading::Document doc;
    ASSERT_EQ(doc.edit, reading::Edit::Viewing);
    ASSERT_FALSE(doc.edit_mode());
    ASSERT_FALSE(doc.edit_seeded());
    ASSERT_FALSE(doc.edit_dirty());
    doc.begin_edit();
    ASSERT_EQ(doc.edit, reading::Edit::Seeding);
    ASSERT_TRUE(doc.edit_mode());
    ASSERT_FALSE(doc.edit_seeded());
    // Seed failure returns to Viewing with the error retained (surfaced
    // as a notice by the caller, not a silent exit).
    doc.fail_edit_seed("Couldn't open x.cpp for editing");
    ASSERT_EQ(doc.edit, reading::Edit::Viewing);
    ASSERT_STREQ(doc.editError, "Couldn't open x.cpp for editing");
    // Successful seed is clean; a mutation makes it dirty.
    doc.begin_edit();
    doc.finish_edit_seed(false);
    ASSERT_EQ(doc.edit, reading::Edit::EditingClean);
    ASSERT_TRUE(doc.edit_seeded());
    ASSERT_TRUE(doc.editError.empty());
    doc.mark_edit_dirty();
    ASSERT_EQ(doc.edit, reading::Edit::EditingDirty);
    ASSERT_TRUE(doc.edit_dirty());
    // A failed save keeps the work dirty in SaveFailed; only a
    // successful save clears it.
    doc.begin_edit_save();
    ASSERT_EQ(doc.edit, reading::Edit::Saving);
    ASSERT_TRUE(doc.edit_dirty());
    doc.fail_edit_save("Couldn't save x.cpp");
    ASSERT_EQ(doc.edit, reading::Edit::SaveFailed);
    ASSERT_TRUE(doc.edit_dirty());
    ASSERT_STREQ(doc.editError, "Couldn't save x.cpp");
    doc.begin_edit_save();
    doc.finish_edit_save();
    ASSERT_EQ(doc.edit, reading::Edit::EditingClean);
    ASSERT_FALSE(doc.edit_dirty());
    ASSERT_TRUE(doc.editError.empty());
    // Stopping retains the buffer; re-entering re-seeds (resumes it).
    doc.mark_edit_dirty();
    doc.stop_edit();
    ASSERT_EQ(doc.edit, reading::Edit::Viewing);
    doc.begin_edit();
    ASSERT_EQ(doc.edit, reading::Edit::Seeding);
    doc.finish_edit_seed(true);
    ASSERT_EQ(doc.edit, reading::Edit::EditingDirty);
}

TEST(watch_name_covers_all_phases) {
    ASSERT_STREQ(std::string(ecs::watch_name(ecs::Watch::Idle)), "Idle");
    ASSERT_STREQ(std::string(ecs::watch_name(ecs::Watch::Swapping)), "Swapping");
    ASSERT_STREQ(std::string(ecs::watch_name(ecs::Watch::Watching)), "Watching");
    ASSERT_STREQ(std::string(ecs::watch_name(ecs::Watch::CoolingDown)), "CoolingDown");
    // The pending scope is an optional: no scope is held while nothing
    // is pending, and a coalesced (merged) scope is what fires.
    std::optional<refresh_scope::Scope> pending;
    ASSERT_FALSE(pending.has_value());
    pending = refresh_scope::merge(pending.value_or(refresh_scope::Scope::None), refresh_scope::Scope::Worktree);
    ASSERT_TRUE(pending.has_value());
    pending = refresh_scope::merge(*pending, refresh_scope::Scope::Index);
    ASSERT_TRUE(refresh_scope::has(*pending, refresh_scope::Scope::Worktree));
    ASSERT_TRUE(refresh_scope::has(*pending, refresh_scope::Scope::Index));
    pending.reset();
    ASSERT_FALSE(pending.has_value());
}

TEST(shelf_state_derivation_and_transition_latch) {
    using Shelf = ecs::LayoutComponent::ShelfState;
    ASSERT_STREQ(std::string(ecs::shelf_state_name(Shelf::Expanded)), "Expanded");
    ASSERT_STREQ(std::string(ecs::shelf_state_name(Shelf::Collapsed)), "Collapsed");
    ecs::LayoutComponent layout;
    ASSERT_FALSE(layout.shelf_collapsed());
    // Starts Expanded with the latch at Collapsed (window opens at shelf
    // width), so the first derivation reports a transition either way.
    ASSERT_TRUE(layout.derive_shelf(false));
    ASSERT_FALSE(layout.shelf_collapsed());
    layout.note_shelf_resized();
    ASSERT_FALSE(layout.derive_shelf(false));
    ASSERT_TRUE(layout.derive_shelf(true));
    ASSERT_TRUE(layout.shelf_collapsed());
    layout.note_shelf_resized();
    ASSERT_FALSE(layout.derive_shelf(true));
}

TEST(picker_position_slots_poll_and_drop_stale) {
    ecs::RepoComponent repo;
    ecs::FilePickerPosition position;
    git::SourcePosition landed;
    landed.location = reading::source("src/app.cpp", {}, 12);
    landed.location.column = 3;
    position.positionRequest.request("go:12", repo_stamp(repo, "go:12"),
        async_work::completed(landed));
    ASSERT_TRUE(position.positionRequest.busy());
    auto polled = position.positionRequest.poll(repo);
    ASSERT_TRUE(polled.has_value());
    ASSERT_EQ(polled->location.line, 12);
    ASSERT_FALSE(position.positionRequest.busy());
    ecs::OutlineResult outline;
    outline.symbols = {{"main", "function", 1}};
    position.symbols.request("src/app.cpp@", repo_stamp(repo, "src/app.cpp@"),
        async_work::completed(outline));
    auto symbols = position.symbols.poll(repo);
    ASSERT_TRUE(symbols.has_value());
    ASSERT_EQ(symbols->symbols.size(), size_t{1});
}

TEST(codeowners_slot_lands_under_its_own_key_unstamped) {
    ecs::RepoComponent repo;
    codeowners::Document doc;
    doc.rules = {{"*.cpp", "@cpp-team"}};
    repo.codeowners.request("repo\nrev", async_work::completed(doc));
    ASSERT_TRUE(repo.codeowners.busy());
    // A completed-but-unconsumed load reads Ready and does not gate idle:
    // the render path is the only poller, and it may never run again.
    ASSERT_STREQ(std::string(*repo.async_slot_phase("codeowners")), "Ready");
    ASSERT_FALSE(repo.find_async_slot("codeowners")->busy(repo));
    auto landed = repo.codeowners.poll_unstamped();
    ASSERT_TRUE(landed.has_value());
    // The landed load keeps its own key even after the lookup key moved on,
    // so the driver can cache it under the right entry.
    ASSERT_EQ(repo.codeowners.key, std::string("repo\nrev"));
    ASSERT_STREQ(std::string(*repo.async_slot_phase("codeowners")), "Ready");
    ASSERT_FALSE(repo.codeowners.busy());
}

int main() { RUN_ALL_TESTS(); }
