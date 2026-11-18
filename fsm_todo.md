# FSM todo — making implicit state machines explicit

Audit of `src/` (2026-11-17). Every claim cites `file:line` as inspected.
Companion to `todo.md`; items here are checkboxed the same way: an item is
done only with its gates green (unit suite, flow suite, screenshots where
the state is user-visible). Nothing in this file changes behaviour until
its own checkbox is worked — each migration is behaviour-preserving by
construction (wrap state, keep rendering reading the same data, delete
bools last).

## The headline

There is no shared async/modal lifecycle type. The codebase has one good
pattern, repeated well in small utilities — and ~24 implicit machines,
mostly in `src/ecs/components.h`, that each re-invent it as
`bool + Task::valid() + string key + generation counter`, polled with
`wait_for(0) == ready` in whichever system renders that panel.

The cost is already visible: the e2e `wait_for_refresh` gate
(`src/main.cpp:750`) hand-enumerates ~30 futures/flags to answer "are we
idle?" — any new machine that forgets to register there hangs or flakes
tests, and "why is it still spinning?" requires reading the poll code of
every panel involved.

## The decision (one pattern, no big-bang)

Add **`src/util/async_slot.h`** (~120 lines, header-only like the rest of
`util/`), modelled on `commit_prefetch::State::observe`
(`src/util/commit_prefetch.h:17`) and `refresh_scope` naming
(`src/util/refresh_scope.h:9`):

```cpp
namespace async_slot {
enum class Phase { Idle, Pending, Ready, Failed };
inline std::string_view name(Phase);  // logs + e2e validate

template <class T, class Key = std::string>
struct Slot {
    Phase phase = Phase::Idle;
    Key key{};  // replaces cacheKey/request/key strings (struct keys where noted)
    reading::RequestStamp stamp{};
    async_work::Task<T> task;
    std::string error;
    void request(Key, reading::RequestStamp, async_work::Task<T>);
    std::optional<T> poll(const RepoComponent&);  // accepts() inside
    void cancel();
    void invalidate();
    bool busy() const { return phase == Phase::Pending; }
};
}
```

Five rules make it pay off, in order of value:

1. **One transition function per machine.** All writes to
   `phase/key/task` go through `request/poll/cancel` — mirroring
   `begin/dismiss/commit_pending_comment` (`components.h:1070`) and
   `focus::State::sync` (`focus_target.h:35`). A grep for `.task =`
   outside the slot is a lint smell.
2. **Transition logging inside the slot only** (via the existing
   `signpost`/log path): `name(old) -> name(new)` per transition. The DX
   win is immediate and free per machine.
3. **AX exposure.** Each `RepoComponent` registers its slots in one span
   of `{name, busy()}`; the e2e idle gate iterates that registry instead
   of enumerating fields, and a `validate async:<name>` key returns
   `name(phase)`. New machines become testable by construction.
4. **UX invariant.** Every `Pending` has a `cancel()` reachable from the
   same Esc/`close_top()` path as other modals, and `Failed` is a
   rendered state — never an empty string tested by truthiness, never an
   empty panel indistinguishable from loading. This kills the
   stuck-spinner class: phases are exhaustive, and `-Wswitch` polices
   what bools cannot.
5. **Keep what already works.** `reading::RequestStamp` stays the
   staleness token; `loading_feedback::Delay` stays spinner timing. Do
   not touch `async_work::Executor` (M26) or `ReadingWorkspace` (M23).

Why this elevates all three experiences at once: UX — no state can be
"stuck" invisibly, failures surface as states with copy and a dismiss;
DX — one header to learn, one transition site to modify per machine,
transitions greppable and loggable; AX — agents/tests address states by
name (`validate async:refresh == Running`) instead of reverse-engineering
flag combinations, and the idle gate stops being a hand-maintained list.

## What "good" already looks like (copy, don't invent)

- `refresh_scope.h:9` — one `enum class`, transitions (`merge`,
  `classify`) and log names in one header.
- `commit_prefetch.h:17` — `State::observe(next, now)`: single transition
  function, cancellation, debounce, counters. The template for the slot.
- `review_anchor.h:10` — named `Status` + exhaustive `label()` switch.
- `focus_target.h:35` — popup-stack transitions in one `sync()` method.
- `reading_workspace.h:162` — the reading core is already explicit
  (`Slot`/`OpenMode`, variant `Location`, `DocumentCycle`). Its
  satellites are not.
- `code_lexer.h:11` — an already-explicit FSM, correctly so.

## Rollout order (checkboxes are worked in this order)

### Wave 0 — the helper

- [x] Add `src/util/async_slot.h` per the decision above, with a unit
      test (`test_async_slot`: request→poll ready, cancel→Idle, stale
      stamp rejected, failed task→Failed with error retained).
      Register it in `tests/run_unit_tests.sh` only.
      Done: `src/util/async_slot.h` (`Phase` + `name()`, `Slot<T,Key>` with
      `request/poll/cancel/invalidate/busy`, `RequestStamp` staleness checked
      inside `poll` via repo path/document/generation/dataGeneration/key,
      transition logging via `signpost::event`); `poll` is a template on the
      repo type so the header stays free of the `components.h`↔`navigation.h`
      cycle and is verified against the real `RepoComponent` as well as the
      unit-test fake. `tests/unit/test_async_slot.cpp` (7 tests) registered
      only in `tests/run_unit_tests.sh`. Gates: `zig build` clean,
      `./tests/run_unit_tests.sh test_async_slot` 7/7 passed,
      `python3 scripts/check_unit_registry.py` ok (64 suites).
- [x] Add the per-`RepoComponent` slot registry + `validate async:<name>`
      key; switch the `main.cpp:750` idle gate to iterate the registry
      (gate behaviour identical: same set of machines, now self-describing).
      Done: `RepoComponent::async_slots()` in `src/ecs/components.h` returns
      a static span of `{name, busy(), phase(), gates_idle}` adapters over
      the existing task/flag fields (25 entries; no state wrapped or moved,
      rendering still reads the same fields). `gates_idle` marks exactly the
      20 conditions the `wait_for_refresh` gate enumerated before (including
      the `valid()`-only `range_diff`/`push_destination` and the conditional
      `untracked_review`/`full_file` guards); non-gating tasks
      (`selected_diff`, `semantic`, `markdown_link`, `full_file_revalidate`,
      `blame_gutter`) are registered with `gates_idle=false` so the gate is
      unchanged. `src/main.cpp` gate now iterates the registry (layout,
      network-ops, review-snapshot and commit-detail checks unchanged), and
      the property getter answers `async:<name>` with `name(phase)`
      (`Idle`/`Pending`/`Ready`; raw tasks cannot name `Failed` without
      consuming them). Gates: `zig build` clean,
      `./tests/run_unit_tests.sh test_async_slot` 14/14 passed (7 new
      registry tests: unique/findable names, idle, pending→busy, ready→not
      busy, valid-only stays busy, non-gating does not hold idle, refresh
      flag), `python3 scripts/check_unit_registry.py` ok (64 suites),
      `check_no_raw_colors.py` + `check_theme_writes.py` ok, e2e
      `tests/e2e_scripts/flow_async_slot_registry.e2e` (new; validates
      `async:*=Idle` after `wait_for_refresh`) plus existing
      `flow_commit.e2e` and `flow_branch_switch.e2e` each run individually
      headless and passed. Note: `test_review_target` fails one history-cap
      test both with and without this change (pre-existing on the base).

### Wave 1 — prove it on clones and stuck-state risks

- [x] **M14 history / commit-search / review-queue panels** → one
      `AsyncSlot` instantiation each. Lives: `components.h:585`
      (file history), `:605` (commit search), `:625` (review queue);
      triplicated `Closed → Open → Loading → List|Error` with poll sites
      at `file_history.h:11`, `commit_search.h:16`,
      `revision_comparison.h:88`. Deletes the most duplicated poll code.
      Effort M (as one refactor), risk low. Preserve: entry types stay distinct.
      Done: `RepoComponent::{fileHistory,commitSearch,reviewQueue}` are now
      `async_slot::Slot<git::GitResult>` in `src/ecs/components.h`; deleted
      `fileHistoryFuture/FutureStamp/Error`, `commitSearchFuture/FutureStamp/Error`,
      and `reviewQueueFuture/FutureStamp/Error/Scope` (scope is now
      `reviewQueue.key`). Poll sites in `src/ui/file_history.h`,
      `src/ui/commit_search.h`, `src/ui/review_queue.h` call `slot.poll(repo)`
      (stale stamps drop to `Idle` inside the slot; the review-queue
      superseded message is restored from the Pending→Idle transition);
      creation in `src/ui/revision_comparison.h` stamps after
      `navigation::open` as before. Rendering still reads the distinct
      `fileHistoryEntries`/`commitSearchEntries` (and `review.queue` for the
      queue); panel-open bools, limits, queries and paths are unchanged.
      Registry entries `file_history`/`commit_search`/`review_queue` now
      report `slot.busy()`/`name(slot.phase)`; `main.cpp` frame pacer and
      `main_content_system.h` read `slot.task`/`busy()`/`error`.
      Gates: `zig build` clean, `./tests/run_unit_tests.sh test_async_slot`
      16/16 (incl. new migrated-slot phase/stale tests) and
      `test_review_store` 25/25, `check_unit_registry.py` +
      `check_no_raw_colors.py` + `check_theme_writes.py` ok, flows run
      individually headless passed: `flow_async_slot_registry.e2e`,
      `improvement_27_commit_search.e2e`, a wait-hardened
      `improvement_25_file_history.e2e` variant (validates
      `async:file_history=Ready`), and the `item_28` review-queue core
      (validates `async:review_queue=Ready`). Note: the stock
      `improvement_25_file_history.e2e` and full `item_28.e2e` fail at
      picker/`approve_hunk_btn` steps identically on clean HEAD (verified
      by reverting this change), so those failures are pre-existing.
- [x] **M21 network ops** → per-op phase + elapsed + cancel.
      `components.h:1466` `PendingNetworkOp`, poll
      `network_ops_system.h:47`. Today fire-and-forget: a hung push is
      indistinguishable from a slow one. Effort S, risk low.
      Preserve: toast-on-complete, refresh only if repo still matches.
      Done: `PendingNetworkOp` in `src/ecs/components.h` now holds an
      `async_slot::Slot<git::GitResult>` (the raw `future` field is
      deleted), `startedAt` + `elapsed()`, `busy()`/`phase()`, and
      `cancel()` via `Task::cancel()` (request-stop; the polling system
      still consumes the cancelled result). `Slot` gained an unstamped
      `request(key, task)` + `poll_unstamped()` + `ready()` in
      `src/util/async_slot.h` — network ops gate on the tab repository
      still matching at completion, not a `RequestStamp`, so the slot is
      unstamped. `src/ecs/network_ops_system.h` enqueues via
      `slot.request(label, task)` and polls via `poll_unstamped()`
      (thrown tasks land in `Failed` with the error toasted instead of
      propagating); toast-on-complete (cancelled/succeeded/failed +
      first stderr line) and refresh-only-if-repo-matches are unchanged,
      labels unchanged. Status bar (`src/ecs/status_bar_system.h`) shows
      `Label... (Ns)` elapsed while Pending and wires its Cancel button
      to `op.cancel()`. Aggregate `NetworkOpsComponent::busy()/phase()`
      backs the new `validate async:network_ops` key in `src/main.cpp`.
      Gates: `zig build` clean, `./tests/run_unit_tests.sh
      test_async_slot` 19/19 (incl. unstamped poll, op phase/elapsed/
      cancel, aggregate phase) and `test_async_task` passed,
      `check_unit_registry.py` + `check_no_raw_colors.py` +
      `check_theme_writes.py` ok, flows run individually headless
      passed: `flow_async_slot_registry.e2e` (validates
      `async:network_ops=Idle`), `flow_push_pull_fetch.e2e`,
      `flow_r7_push_menu.e2e`, `flow_r8_pull_menu.e2e`,
      `flow_r9_fetch_menu.e2e`, `flow_t5_push_toolbar.e2e`,
      `flow_t6_pull_toolbar.e2e`, `flow_t7_fetch_toolbar.e2e`,
      `flow_s2_push_sidebar.e2e`, `flow_s3_pull_sidebar.e2e`,
      `flow_commit.e2e`.
- [x] **M2 commit-log paging + history scope** → `LogPhase` on
      `CommitLogPage` (`components.h:293`, `:441`); `hasMore` computed
      once from returned count vs page size instead of `size == 100`
      inferred in three places (`async_git_refresh_system.h:316,432,445`).
      Effort S, risk low. Preserve: 100-page size, HEAD pinning (`:459`).
      Done: `LogPhase {Idle, LoadingScope, LoadingPage, Error}` +
      `log_phase_name()` + `commit_log_has_more(returned, requested)` and
      `CommitLogPage {phase, hasMore, requestedPageSize, busy/request_page/
      begin_scope/finish/fail}` in `src/ecs/components.h`; single page-size
      constant `git::kCommitLogPageSize = 100` in `src/git/history_scope.h`
      (drives `-100` in `history_scope_args`). Deleted
      `RepoComponent::commitLogHasMore/commitLogLoading/commitLogLoaded`
      and `CommitLogPage::requested` (replaced by `phase==LoadingPage`);
      rendering still reads `commitLog` (sidebar/probe now read
      `commitLogPage.hasMore/phase/error`). `hasMore` is computed once in
      `CommitLogPage::finish(returned)` at the three former inference
      sites (refresh `pf.log`, `update_history_scope`,
      `update_history_page` in `src/ecs/async_git_refresh_system.h`) plus
      the e2e seed in `e2e_command_handlers.h`; HEAD pinning for
      `Current`-scope paging is unchanged. Registry entries
      `commit_log_page`/`history_scope` now return `log_phase_name(phase)`.
      Gates: `zig build` clean, `./tests/run_unit_tests.sh test_async_slot`
      23/23 (incl. 4 new LogPhase/hasMore/registry tests),
      `test_refresh_lifetime` 3/3, `test_refresh_scope` 5/5,
      `test_history_selection` 4/4, `check_unit_registry.py` +
      `check_no_raw_colors.py` + `check_theme_writes.py` ok, flows run
      individually headless passed: `flow_async_slot_registry.e2e`
      (validates `async:commit_log_page=Idle` + `async:history_scope=Idle`),
      `flow_commit_log_scroll.e2e`, `flow_history_selection.e2e`,
      `flow_browse_commits.e2e`, `flow_refresh.e2e`, `flow_commit.e2e`.
- [x] **M22 settings save** → `Save` enum + one `apply_write_result()`.
      `settings.h:126` (`pendingSave_`, `writeFuture_`, latch
      `loadError`), `settings.cpp:248`. Blocked saves are currently
      silent in the UI. Effort S, risk low. Preserve: 250 ms debounce,
      never-overwrite-on-load-error.
      Done: `Settings::Save {Clean, Pending, Writing, Blocked, Failed}` +
      `Settings::name(Save)` + `save_state()` in `src/settings.h`;
      `saveState_` transitions in `src/settings.cpp` (`Pending` on
      `save_if_auto`, `Writing` on worker launch, `Blocked` on load-error
      at load and at both write attempts, `Failed`/`Clean` only via the
      single `apply_write_result(Save, error)`, which also owns
      `saveWriteCount_`/`saveError`/error logging). Sync
      `write_save_file()` drain, sync direct write, async
      `flush_pending_save()` completion and both blocked branches all go
      through `apply_write_result()`; `Blocked`/`Failed` surface via the
      existing `saveError` status-bar path, no new UI. 250 ms debounce
      (`pendingSave_` deadline unchanged) and never-overwrite-on-load-error
      are preserved; `pendingSave_`/`writeFuture_` remain the deadline/task,
      no bool was kept alongside the enum. Gates: `zig build` clean,
      `./tests/run_unit_tests.sh test_settings` 37/37 (5 new: name(),
      Pending→Writing→Clean debounce, Blocked sync+async never-overwrite
      + recovery, Failed sync+async + recovery, sync-drains-async),
      `python3 scripts/check_unit_registry.py` +
      `check_no_raw_colors.py` + `check_theme_writes.py` ok, flows run
      individually headless passed: `flow_settings_preferences.e2e`,
      `flow_light_theme.e2e`. Note: `improvement_48_code_font.e2e` fails on
      stale font-size expectations (expects 18/24, code snaps to 14/16
      tiers per `test_settings`) unrelated to this change.

### Wave 2 — the money and data-loss surfaces

- [x] **M6 commit-detail patch** → `AsyncSlot<CommitPatch, PatchKey>`
      with `PatchKey{repo, commit, parent, context, ignoreWs}` as a
      struct (identity is currently 5 hand-compared fields,
      `components.h:682`, `commit_detail.h:85`, duplicated in
      `navigation.h:238`). Effort M, risk med. Preserve peek/revalidate.
      Done: `ecs::PatchKey{repo, commit, parent, context, ignoreWs}` with
      defaulted struct equality in `src/ecs/components.h`;
      `CommitDetailRuntime::patch` is now
      `async_slot::Slot<CommitPatch, PatchKey>` and the five cached fields
      (`cachedCommitHash/cachedParentHash/cachedRepoPath/cachedContext/
      cachedIgnoreWhitespace`), `requestStamp`, `patchFuture` and
      `revalidateFuture` are deleted. `src/ui/commit_detail.h` requests
      via `patch.request(desiredKey, stamp, …)`, polls via
      `patch.poll(repo)` (stale-ready drops return early as before), and
      retargets `patch.key`/`patch.stamp` after `resolve_review` so a
      resolved ref does not reload. Peek-then-revalidate is preserved:
      a cache peek is published at once with `patchRevalidating=true`
      while the same slot task validates behind it; `patch_loading()`
      (`task.valid() && !patchRevalidating`) reproduces every former
      `patchFuture.valid()` reader (spinner, diff render, sidebar,
      `change_navigation`, `review_queue`, reading probe, status bar,
      frame pacer, `wait_for_refresh`), so revalidation never shows as a
      cold load and its trace is still discarded on arrival. All other
      readers (`navigation::release_inactive_review`, `repo_search`,
      `file_picker`, `main_content` anchor, review-queue stale check,
      menu/e2e invalidations via `patch.invalidate()`) now compare or
      clear `patch.key`. `validate async:commit_patch` in `src/main.cpp`
      returns `name(patch.phase)`. Gates: `zig build` clean,
      `./tests/run_unit_tests.sh test_async_slot` 27/27 (4 new:
      PatchKey equality over all five fields, slot poll→Ready with key,
      revalidating-is-not-cold-load, stale stamp→Idle),
      `test_commit_patch` 8/8, `test_review_store` 25/25,
      `check_unit_registry.py` + `check_no_raw_colors.py` +
      `check_theme_writes.py` ok, flows run individually headless
      passed: `flow_commit_detail_regression.e2e` (new
      `wait_until 30 validate async:commit_patch=Ready`; its re-select
      exercises the peek/cache-hit path), `flow_browse_commits.e2e`,
      `flow_async_slot_registry.e2e`, `flow_commit.e2e`,
      `flow_commit_file_open.e2e`, `flow_commit_then_browse.e2e`,
      `flow_commit_log_scroll.e2e`. Note: `test_review_target` passes the
      migrated inactive-release test but still fails the pre-existing
      history-cap test noted under Wave 0.
- [x] **M17 review session store** → `ReviewStore` phases incl.
      `SaveFailed` surfaced in the basket header. `components.h:732`
      (`reviewing`, `dirty`, `nextSaveAttempt` retry loop at `:769`),
      `review_store.cpp:122` (`loadError` blocks all future saves with no
      recovery transition). The only machine where an implicit state can
      lose user work silently. Effort M, risk med.
      Preserve: JSON schema v2, scope keying.
      Done: `ecs::ReviewStorePhase {Unloaded, Loading, Ready, Dirty,
      Saving, SaveFailed, LoadFailed}` + `review_store_phase_name()` in
      `src/ecs/components.h`; `ReviewComponent::storePhase/storeError`
      replace the deleted `dirty` bool and `loadError` string (all
      mutations call `mark_dirty()`, `dirty()` derives from
      Dirty/SaveFailed, and SaveFailed/LoadFailed stay visible until a
      retry succeeds). `src/review_store.cpp`: `load_review` drives
      Loading→Ready/LoadFailed (failed loads preserve in-memory and on-disk
      state and block saves by phase), `persist_review` is the single
      Saving→Ready/SaveFailed transition (unsaved work stays dirty),
      legacy-migration save failure is SaveFailed, and the new
      `review_store::retry_load()` recovery transition re-loads out of
      LoadFailed (still-corrupt stays blocked; a gone file returns
      preserved work as Dirty so persist can write it). JSON schema v2,
      scope keying and draft restore are unchanged. Basket footer label
      (`basket_path_hint`, existing label area) renders SaveFailed /
      LoadFailed / Saving, with a `basket_retry_load` button in
      LoadFailed; status bar reads the phase; `validate
      async:review_store` names the phase in `src/main.cpp`. Gates:
      `zig build` clean, `./tests/run_unit_tests.sh test_review_store`
      29/29 (4 new: phase names, Dirty→Saving→Ready, SaveFailed retains
      dirty and recovers, LoadFailed retry blocks-then-unblocks, async
      Loading→Ready) and `test_async_slot` 27/27,
      `check_unit_registry.py` + `check_no_raw_colors.py` +
      `check_theme_writes.py` ok, flows run individually headless passed:
      `flow_async_slot_registry.e2e` (validates
      `async:review_store=Ready`), `ballroom_review.e2e`,
      `improvement_35_comment_jump.e2e`. Note: several
      `improvement_3x` feedback flows fail at their first
      `comment_hunk_btn` click (that button is gated on review mode,
      which those scripts never toggle) identically with this change's
      store logic untouched; the toggling `ballroom_review.e2e` passes.
- [x] **M16 commit editor flow** → `CommitFlow` enum; replace the
      one-frame `commitRequested` flag (consumed in 4 places,
      `sidebar_system.h:190`) with `request_commit()`. `components.h:1377`.
      Effort S, risk low-med — test well, it's the write path.
      Preserve: split-button one-shot policy override.
      Done: `ecs::CommitFlow {Editing, AwaitingPolicy, Committing, Done}`
      + `commit_flow_name()` on `CommitEditorComponent` in
      `src/ecs/components.h`; deleted `commitRequested` and
      `showUnstagedDialog` (dialog open is exactly `flow==AwaitingPolicy`,
      bound via a local in `render_unstaged_dialog`). All entry points
      (Enter submit, Commit button, split button, Repository menu,
      toolbar) call `request_commit()` (Editing/Done→Committing,
      idempotent while pending/dialog/in-flight, never touches the
      override); the sidebar handler consumes a pending request
      (`commit_requested()`: Committing without a future) exactly as
      the flag handler did — no-staged stays pending, both-present
      reads and resets `commitPolicyOverride` only on that path, Ask
      → `await_policy()`. `begin_commit` → Committing on launch /
      Editing on template refusal, dialog choices launch via it,
      Cancel/Esc → `dismiss_policy()`→Editing, `poll_commit` success
      → Done / failure → Editing, review-workspace suppression via
      `cancel_request()` (never cancels an in-flight future), and
      `app_reset` → Editing. `validate async:commit_flow` in
      `src/main.cpp` returns `commit_flow_name(flow)`. Gates:
      `zig build` clean, `./tests/run_unit_tests.sh test_async_slot`
      31/31 (4 new: name coverage, pending-until-launched + override
      preserved, dialog/Done transitions, cancel-never-cancels-in-flight),
      `test_commit_message` 3/3, `test_commit_patch` 8/8,
      `check_unit_registry.py` + `check_no_raw_colors.py` +
      `check_theme_writes.py` ok, flows run individually headless
      passed: `flow_commit.e2e` (validates `async:commit_flow=Editing`
      → `Done`), `flow_commit_then_browse.e2e`,
      `flow_commit_amend.e2e`, `flow_commit_file_open.e2e`,
      `flow_r3_commit_menu.e2e`, `flow_r4_amend_commit.e2e`,
      `flow_async_slot_registry.e2e` (validates
      `async:commit_flow=Editing`).
- [x] **M19 review baseline** → `Baseline` enum, snapshot as
      `optional<string>`. `components.h:779`; `snapshotCapturing`
      duplicates `snapshotFuture.valid()` and can disagree with it.
      Effort S, risk low.
      Done: `ecs::Baseline {None, Capturing, Ready, Failed}` +
      `baseline_name()` in `src/ecs/components.h`; `baselineSnapshot`
      is now `std::optional<std::string>` and the duplicate
      `snapshotCapturing` bool is deleted. Transitions
      `begin_baseline_capture()`/`finish_baseline_capture()`/
      `finish_baseline_compare()`/`fail_baseline()` on
      `ReviewComponent` are called from `src/ui/review_snapshot.h`
      start/poll (a failed capture/compare keeps any held snapshot);
      rendering still reads `baselineSnapshot`/`snapshotFuture`/
      `snapshotError` as before. `validate async:baseline` in
      `src/main.cpp` returns `baseline_name(baseline)`; the reading
      probe exposes `baseline_phase`. Gates: `zig build` clean,
      `./tests/run_unit_tests.sh test_review_store` 31/31 (incl.
      `baseline_phase_names_and_transitions`) and `test_async_slot`
      31/31, `check_unit_registry.py` + `check_no_raw_colors.py` +
      `check_theme_writes.py` ok, flows run individually headless
      passed: `flow_async_slot_registry.e2e` (validates
      `async:baseline=None`), `flow_snapshot_selection.e2e`,
      `ballroom_review.e2e`. Note: `baseline_all_flows.e2e` fails on
      unrelated sidebar `click_text` targets (Staged/Untracked/
      TODO.md), not on baseline/composer steps.
- [x] **M18 comment composer** → `Composer` enum with
      `variant<HunkKey, CommentIndex>` target. `components.h:748`
      (12 composing fields) + `:1070` transition trio (keep them as the
      sole mutators). Effort S, risk low. Preserve draft-on-dismiss.
      Done: `ecs::Composer {Closed, Composing, EditingExisting}` +
      `composer_name()` in `src/ecs/components.h`;
      `ReviewComponent::{composer, composerTarget, composerDraft,
      composerFocus}` replace the deleted 12 composing fields and the
      `editingComment` trio — `composerTarget` is
      `std::variant<HunkKey, CommentIndex>`, `composerDraft` (a
      `Comment`) is the shared text/kind/location buffer, with
      `composing_key()`/`editing_index()` accessors and
      `on_comment_erased()` shifting/closing an editing target.
      `begin_comment` (both overloads)/
      `dismiss_pending_comment`/`commit_pending_comment` in
      `components.h` are the sole phase mutators (load/restore in
      `review_store.cpp` moves the fields wholesale); draft-on-dismiss
      is preserved via `preserve_composing_draft` (empty text drops
      the draft, otherwise it is kept in `drafts`). Rendering still
      reads `composer`/`composerDraft` in `diff_renderer.h`,
      `main_content_system.h`, `review_comment_kind.h`, `focus.h`.
      `validate async:composer` in `src/main.cpp` returns
      `composer_name(composer)`; the reading probe exposes
      `composer`. Gates: `zig build` clean,
      `./tests/run_unit_tests.sh test_review_store` 31/31 (incl.
      `composer_phase_names_targets_and_draft_on_dismiss`) and
      `test_async_slot` 31/31, `check_unit_registry.py` +
      `check_no_raw_colors.py` + `check_theme_writes.py` ok, flows
      run individually headless passed:
      `flow_async_slot_registry.e2e` (validates
      `async:composer=Closed`), `ballroom_review.e2e`,
      `improvement_35_comment_jump.e2e`,
      `improvement_33_edit_comments.e2e`,
      `improvement_36_drafts.e2e`,
      `improvement_32_line_comments.e2e`.

### Wave 3 — the modal archetype and its modes

- [x] **M8 file picker + M9 picker modes (together)** → `Picker` phase
      enum + `PickerMode { Files, Line, Symbol, ProjectSymbol, Commands }`
      as one field. Lives: `FilePickerScope` ~25 fields
      (`components.h:1198`), open/preview/restore flags (`:1291`), mode
      bools `lineMode/commandMode/symbolMode/projectSymbolMode`
      (`components.h:1235`, 16 combinations of which 4 are legal).
      De-facto: `Closed → Open(preview saved) → CatalogLoading →
      Matching(debounced) → Previewing → Confirm | Cancel(restore)`,
      plus `confirmWhenReady` for Enter-before-match. Sub-decisions:
      drop the picker-local `searchId/matchSearchId` pair in favour of
      `RequestStamp`; one `open(mode)/confirm()/cancel()` owns the
      restore point so every open path sets it. Effort L, risk med.
      Preserve: previous results stay visible during match; per repo+HEAD
      source cache (`file_picker.h:18`); live preview semantics per mode.
      Done: `ecs::PickerPhase {Closed, Browsing, Matching, Previewing,
      Confirming}` + `picker_phase_name()`, `ecs::PickerMode {Files, Line,
      Symbol, ProjectSymbol, Commands}` + `picker_mode_name()`, and
      `ecs::Picker` (phase, the single mode field, restore point,
      previewKey, confirmKeep) on `LayoutComponent` in
      `src/ecs/components.h`; deleted `LayoutComponent::filePickerOpen/
      filePickerPreviewing/filePickerRestoreLocation/
      filePickerRestoreAnchor/filePickerPreviewKey/filePickerResults`
      and `FilePickerPosition::{lineMode,commandMode,symbolMode,
      projectSymbolMode}` (render dispatch and the palette now switch on
      `picker.mode`). `LayoutComponent::open_picker(mode, repo)` is the
      one open path — every opener (Cmd+P, Go to File/Line/Symbol/
      ProjectSymbol, command palette, catalog, breadcrumb) goes through
      it and it saves the restore point; `confirm_picker()` discards it,
      `cancel_picker()` keeps it for `FilePickerSystem`'s exact restore
      (modal-Esc, Esc popup, tab switch, repo-picker handoff all cancel;
      confirmed navigation and palette runs confirm). The
      `searchId/matchSearchId` pair and `matchKey` are deleted:
      `FilePickerScope::matchStamp` (`reading::RequestStamp`, key = the
      match key) is stamped in `launchMatch` and a landed match applies
      only while `navigation::accepts` it, in `src/ui/file_picker.h`;
      `confirmWhenReady/confirmKeep` are deleted — Enter-before-match is
      `PickerPhase::Confirming` (+ `confirmKeep`), consumed by the landed
      match. Previous results stay during Matching, the per repo+HEAD
      `picker_source_cache` is untouched, and live preview per mode is
      `begin_preview(key)` (Files entry key, `line:L:C`, `symbol:N`) with
      Escape restoring the saved location+anchor verbatim.
      `validate async:picker` / `async:picker_mode` in `src/main.cpp`
      return the phase/mode names. Gates: `zig build` clean,
      `./tests/run_unit_tests.sh test_async_slot` 36/36 (5 new: phase/
      mode names, open saves restore + single mode, Matching/
      Previewing/Confirming transitions, confirm-discards vs
      cancel-keeps restore, match stamp) and `test_repo_picker` 3/3,
      `check_unit_registry.py` + `check_no_raw_colors.py` +
      `check_theme_writes.py` ok, flows run individually headless
      passed: `flow_quick_open_preview.e2e` (validates
      `async:picker=Closed` after Esc-restore), `flow_outline_panel.e2e`,
      `flow_command_palette.e2e` (validates `async:picker=Closed`),
      `flow_repo_picker.e2e`, `flow_sticky_scopes.e2e`,
      `flow_symbol_picker.e2e`, `flow_async_slot_registry.e2e`
      (validates `async:picker=Closed` + `async:picker_mode=Files`).
- [x] **M28 repo picker** → fold into the M8 shell (own field set today:
      `components.h:1281` plus the welcome `repositoryPicker*` at `:1275`).
      Effort M, risk low. Depends on M8.
      Done: `PickerMode::Repositories` on the shared `Picker` shell in
      `src/ecs/components.h`; the deleted `repoPickerOpen/Query/Index/Focus/
      Future/Resolved/ResolveKey` and welcome `repositoryPickerQuery/Index/
      FocusPending/Alphabetical` are replaced by one `RepoPickerScope
      repoPicker` (shared query/index/focusPending, welcome `alphabetical`,
      resolve future/resolved/key) with shared `clamp_index/apply_keys`
      navigation used by both surfaces. `LayoutComponent::
      repo_picker_open()/open_repo_picker_shell()` own modal open (Browsing
      in Repositories mode, focus + selection reset, query kept);
      `src/ui/repo_picker.h` confirms on activate (missing → relink as
      before), cancels on modal Esc/backdrop, and polls
      `repoPicker.future`; repo-specific entry building
      (`repo_picker::build/rank`, sections, worktree grouping, MRU-tab
      collection) is unchanged, as are Cmd+Alt+O and the MRU switcher gate
      (now just `picker.is_open()`). Quick Open's Repositories category
      restores a live preview inline, keeps its navigation effect from
      closing the new mode (`keepPicker`), confirms, and hands off;
      `FilePickerSystem` leaves Repositories mode to `RepoPickerSystem`.
      Welcome rendering reads the same scope. `validate async:picker` /
      `async:picker_mode` name `Browsing`/`Repositories` while open.
      Gates: `zig build` clean, `./tests/run_unit_tests.sh test_async_slot`
      38/38 (incl. shell open/confirm/cancel and shared-navigation tests)
      and `test_repo_picker` 3/3, `check_unit_registry.py` +
      `check_no_raw_colors.py` + `check_theme_writes.py` ok, flows run
      individually headless passed: `flow_repo_picker.e2e` (now validates
      `async:picker=Browsing` + `async:picker_mode=Repositories` open and
      `async:picker=Closed` after switch), `flow_quick_open_preview.e2e`,
      `flow_command_palette.e2e`, `flow_async_slot_registry.e2e`,
      `flow_tab4_welcome.e2e`, `flow_open_repo.e2e`, plus throwaway
      handoff (Quick Open → Repositories → Esc) and welcome-tab
      Cmd+Alt+O → Esc probes in `/tmp`.
- [x] **M24 modal constellation** → promote `focus::Popup` from a derived
      view to stored state: `LayoutComponent::modal: optional<Popup>` +
      per-dialog payloads, `close_top()` for Esc, `open_popups()` becomes
      a projection then is deleted. Writers today set ~15 open-bools
      independently (`components.h:399` push dialog, `:1158` branch
      dialogs, `:1288` relink, `:1340` shortcuts/options), so two modals
      can be "open" with precedence decided by read ordering — the
      highest stuck-UX risk in the app (a leftover bool leaves an input
      gate installed). Dialog by dialog, after M8. Effort L, risk med.
      Preserve: focus-return stack (`focus_target.h:47`).
      Done: `LayoutComponent::modal` (`optional<focus::Popup>`, stashed
      per tab owner like focus) + `open_modal/close_modal/close_top_modal`
      in `src/ecs/components.h` is the one stored open-dialog state;
      deleted `pushDialogOpen`, `comparisonEditorOpen`, `relinkOpen`,
      `shortcutsOpen`, `diffOptionsOpen` and the four branch/stash bools.
      Payloads: `RepoComponent::pushDialog` (`PushDialogPayload`),
      `BranchDialogState` (data only), `LayoutComponent::relinkDialog`
      (`RelinkDialogPayload`); comparison payload stays on the repo and
      editor-open is passed as `comparisonOpen(editorOpen)`. `Popup`
      gained NewBranch/DeleteBranch/ForceDeleteBranch/Stash +
      `popup_name()`; `ui::stored_popups()` in `src/ui/focus.h` projects
      stored state for `focus::State::sync` (return stack untouched) and
      `open_popups()` is deleted; the Esc switch in
      `main_content_system.h` is the single `close_top_popup()`.
      `validate async:modal` names the open dialog (or `None`).
      Gates: `zig build` clean, `./tests/run_unit_tests.sh
      test_async_slot` 41/41 (3 new modal tests) and `test_focus_target`
      10/10, `check_unit_registry.py` + `check_no_raw_colors.py` +
      `check_theme_writes.py` ok, flows run individually headless
      passed: `flow_branch_create.e2e`, `flow_delete_branch.e2e`,
      `flow_stash.e2e`, `improvement_50_shortcuts.e2e`,
      `flow_r7_push_menu.e2e`, `flow_push_pull_fetch.e2e`,
      `flow_t5_push_toolbar.e2e`, `flow_commit.e2e`,
      `flow_async_slot_registry.e2e`, plus `/tmp` probes validating
      `async:modal` open→Esc→`None` (gate released) for Shortcuts,
      NewBranch and the comparison editor. Notes: `test_review_target`
      still fails only the pre-existing history-cap test documented
      under Wave 0; `reading_navigation_boundary.e2e` fails at its
      `native_menu_action` step (headless native menu unavailable);
      no stock flow opens the push dialog surface (menu/sidebar Push
      enqueue or early-return in a review workspace), so push-dialog
      Esc is covered by the shared `close_top_popup` path and unit
      tests, not a dedicated flow.
- [x] **M12 find-in-file** → `Find { Closed, Editing, Scanning }`.
      `reading_workspace.h:197` (three bools) + source-scan runtime
      `components.h:346`; diff find (sync) and source find (async) share
      UI with divergent close paths. Effort S, risk low.
      Done: `reading::Find {Closed, Editing, Scanning}` +
      `find_name()` and `FindState {phase, is_open()/scanning(),
      open()/close()/sync_scanning()}` in
      `src/util/reading_workspace.h` replace the deleted `open` bool
      (diff find stays Editing; source find is Scanning only while
      its scan is in flight; `focus`/`navigate` remain one-shot
      transients and `pendingStep` remains data accumulated while
      Scanning, applied then cleared when the scan lands, exactly as
      before). `ecs::SourceFindRuntime` in `src/ecs/components.h` is
      now `{async_slot::Slot<SourceFindResult> slot, result}` — the
      former `key/request/future` fields are deleted; the renderer
      in `src/ui/diff_renderer.h` requests via `slot.request(key,
      stamp, …)`, polls via `slot.poll(repo)` (a stale drop clears
      the key so the next frame relaunches, as the old code did, and
      a `Failed` task surfaces in the notice via `result.error`),
      and syncs the find phase from `slot.busy()` each frame.
      Unified close: `navigation::close_find` (Esc via
      `close_top_popup` and the bar's close button) cancels the scan
      via `cancel_source_find` then goes Closed, and vice versa —
      `cancel_source_find` demotes Scanning→Editing, and leaving a
      document (`finish`→`release_source`, which clears the runtime)
      demotes the departed document's find the same way, so no find
      is ever left Scanning with no scan in flight. Rendering still
      reads `sourceFind.result`. The registry `source_find` entry
      reports `slot.busy()`/`name(slot.phase)`, and
      `validate async:find` in `src/main.cpp` returns
      `find_name(phase)`; the reading probe exposes both phases.
      Gates: `zig build` clean, `./tests/run_unit_tests.sh
      test_async_slot` 45/45 (4 new: find names, phase transitions
      preserving pendingStep, source-find slot poll→Ready + registry
      phases + cancel-retains/clear, stale stamp→Idle) and
      `test_diff_tools` 57/57 (incl. updated per-document find test
      and new close-cancels-scan / scan-cancel-demotes test),
      `check_unit_registry.py` + `check_no_raw_colors.py` +
      `check_theme_writes.py` ok, flows run individually headless
      passed: `flow_async_slot_registry.e2e` (now validates
      `async:find=Closed` + `async:source_find=Idle`),
      `improvement_50_shortcuts.e2e`, `flow_occurrence_highlight.e2e`,
      plus a `/tmp` source-find probe (quick-open a source file,
      Cmd+F→Editing, scan→`async:source_find=Ready`, Esc→
      `async:find=Closed` + `async:source_find=Idle`). Note:
      `improvement_11_find.e2e` fails at its commit-view
      `diff_find_input` step identically with this change's find
      hunks reverted on the same tree (verified by reverting,
      rebuilding and rerunning), so that failure is pre-existing.

### Wave 4 — refresh core and the big readers

- [x] **M1 async git refresh** → `RefreshPhase { NeverLoaded, Queued,
      Running, Settled }` over the existing per-read sub-state
      (`PendingFutures`, `async_git_refresh_system.h:488`). Completion is
      currently "all optionals empty" (`:396`); `commitLogLoading` is set
      from three places and can clear early. Effort M, risk med — every
      panel depends on it, so it comes after its feeders are slotted.
      Preserve: prefetch adoption (`:150`), scope→plan mapping,
      `dataGeneration` bump only on status change (`:292`).
      Done: `ecs::RefreshPhase {NeverLoaded, Queued, Running, Settled}` +
      `refresh_phase_name()` in `src/ecs/components.h`;
      `RepoComponent::refreshPhase` replaces the deleted
      `refreshRequested`/`isRefreshing`/`hasLoadedOnce` bools.
      `request_refresh()`/`request_refresh(scope)` are the one writer
      entry (every former flag writer — menu, toolbar, sidebar, watcher,
      tab switch, network ops, source edit, commit, e2e seed — calls it;
      scoped requests merge into the queued scope), `begin_refresh()`
      is Queued→Running and consumes the queued scope (None→Full, the
      scope→plan mapping itself unchanged), and the refresh poll in
      `src/ecs/async_git_refresh_system.h` is the only other driver:
      `finish_refresh()` (Running→Settled, or →Queued when a request
      arrived mid-run, its scope retained), `requeue_refresh()` on
      repository change, `abandon_refresh()` on a dropped run. The
      monotonic loaded-once latch is written only by those transitions
      (`note_refresh_loaded` at status-ready, `finish_refresh` at drain),
      so re-refreshes keep `has_loaded_once()` through Queued/Running.
      Completion is now `PendingFutures::done()` (all per-read futures
      consumed); prefetch adoption, the status-change-only
      `dataGeneration` bump, warming of restored tabs and the commit-log
      `LogPhase` handling are untouched. Rendering still reads the same
      repo data; gates/readers read `refresh_busy()`/`refreshing()`/
      `has_loaded_once()`. Registry `refresh` entry and
      `validate async:refresh` now return `refresh_phase_name(phase)`.
      Gates: `zig build` clean, `./tests/run_unit_tests.sh
      test_async_slot` 49/49 (4 new: phase names, request→begin→finish
      + loaded latch, request-while-Running queues next run,
      requeue/abandon), `test_refresh_lifetime` 3/3 (now asserting
      Running→Settled), `test_refresh_scope` 5/5,
      `test_history_selection` 4/4, `check_unit_registry.py` +
      `check_no_raw_colors.py` + `check_theme_writes.py` ok, flows run
      individually headless passed: `flow_async_slot_registry.e2e`
      (validates `async:refresh=Settled`), `flow_refresh.e2e`,
      `flow_keyboard_refresh.e2e`, `flow_tab3_switch_tab.e2e`,
      `flow_browse_commits.e2e`, `flow_browse_files.e2e`,
      `flow_commit.e2e`, `flow_sidebar_controls.e2e`,
      `flow_commit_log_scroll.e2e`, `flow_history_selection.e2e`,
      `flow_file_watcher.e2e`, `flow_branch_switch.e2e`,
      `flow_open_repo.e2e`.
- [x] **M4 untracked review** → named sub-read of M1 (or a slot): the
      `optional<unsigned>` generation-as-dirty-flag (`components.h:460`)
      with three reset writers is the least discoverable idiom in the
      file. Effort S, risk low. Preserve append-dedupe by path.
      Done: `ecs::UntrackedReviewPhase {Stale, Loading, Ready}` +
      `untracked_review_phase_name()` and `ecs::UntrackedReviewRead`
      (`phase/task/generation/repository/notice` with `begin/settle/poll/
      invalidate/current_for/needs_read/stale_in_flight`) in
      `src/ecs/components.h` replace the deleted
      `untrackedReviewFuture/untrackedReviewGeneration/
      untrackedReviewRepository/untrackedReviewNotice`. The refresh poll
      in `src/ecs/async_git_refresh_system.h` is the only driver —
      stale in-flight reads are invalidated, landed reads are polled and
      appended deduped by path exactly as before (patchGeneration bump
      unchanged), and the next read begins (or settles when there are no
      untracked files) only once the repo has loaded; the landed-diff
      and e2e-seed resets both go through `invalidate()`/default-reset.
      Rendering still reads `currentDiff` and the read's `notice`;
      `untrackedReviewLoading` spinner timing is untouched. Registry
      `untracked_review` reports `untracked_review_phase_name(phase)`.
      Gates: `zig build` clean, `./tests/run_unit_tests.sh
      test_async_slot` 55/55 (6 new: phase names, read lifecycle +
      generation staleness, stale-in-flight invalidation, DiffSource
      names, begin→land→use-whole, failure-not-previous-diff) and
      `test_content_reader` 34/34, `test_refresh_lifetime` +
      `test_refresh_scope` + `test_review_store` passed,
      `check_unit_registry.py` + `check_no_raw_colors.py` +
      `check_theme_writes.py` ok, flows run individually headless
      passed: `flow_async_slot_registry.e2e` (validates
      `async:untracked_review=Ready` + `async:selected_diff=Whole`),
      `ballroom_review.e2e`, `flow_refresh.e2e`, `flow_commit.e2e`, and
      `python3 tests/large_diff.py --output /tmp/fh-ld-fsm` PASS
      (first selected-file diff visible on a 2000-file repo).
- [x] **M3 selected-file diff** → `DiffSource { Whole, SinglePending,
      Single, Failed }` replacing the `~0u` sentinel generation compare
      (`components.h:474`, `main_content_system.h:1135`); a failed single
      diff currently leaves the previous diff shown silently.
      Effort S, risk low.
      Done: `ecs::DiffSource {Whole, SinglePending, Single, Failed}` +
      `diff_source_name()` and `ecs::SelectedDiffRead`
      (`source/key/task/diff/error` with `begin/take/land/fail/
      use_whole`) in `src/ecs/components.h` replace the deleted
      `selectedDiffKey/selectedDiffFuture/selectedDiff`;
      `currentDiffGeneration/stagedDiffGeneration` are now
      `std::optional<unsigned>` (empty = not landed) instead of the
      `~0u` sentinel. `src/ecs/main_content_system.h` drives the one
      transition site: whole-repo readiness from the optional
      generations, a ready single read lands as `Single` (no matching
      file = empty, as before), a non-success or thrown read lands as
      `Failed` with the stderr/exception retained, and a landed whole
      diff takes over via `use_whole()`. `Failed` renders a dedicated
      `diff_failed` panel ("Couldn't load the diff for this file" +
      error) instead of silently keeping the previous diff. Registry
      `selected_diff` (still non-gating) reports
      `diff_source_name(source)`. Gates: same build/unit/check/flow
      evidence as M4 above, including the `large_diff` harness. Note:
      no stock flow forces a single-file `git diff` failure, so the
      `Failed` panel is covered by the unit tests above, not a flow.
- [x] **M5 full-file paged reading** → `FileLoad` enum + `PageKey`
      struct replacing both concatenated cache-key strings (built in two
      places, `full_file_view.h:127` vs `:183` — drift means infinite
      reload or a stale page; the "Source changed while scrolling" error
      is the visible symptom). Largest machine in the app
      (`components.h:481-530`, `:584`). Effort L, risk high — hottest
      render path, goes late and behind its own flow tests.
      Preserve: peek-then-revalidate, bounded pages, spinner delay.
      Done: `ecs::FileLoad {Idle, Loading, Revalidating, Extending,
      Ready, Failed}` + `file_load_name()` and `ecs::PageKey {source,
      encoding, action, cursor, target}` with defaulted `==` (plus
      `FileSource`/`PageTarget` and `make_file_source`/`make_page_key`,
      which carry the data generation only for empty/INDEX revisions
      exactly as the old source string did) in `src/ecs/components.h`;
      the deleted `fullFileCacheKey`/`fullFileSourceKey` strings (the
      `:185` resolved-revision rebuild now retargets `key.source` and
      `load.source` in the poll), `fullFileFuture`/`fullFileRevalidate`/
      `fullFileError`/`fullFileLoading`/`fullFileRequestStamp`/
      `fullFilePageRequest`/`fullFileExtendRequest` are one
      `ecs::FullFileLoad fullFileLoad` (phase, source, key, request,
      task, revalidate, stamp, 150 ms `loading` delay, error) whose
      `begin/land/fail/settle_revalidation/drop_stale` transitions run
      only in `render_full_file` (`src/ui/full_file_view.h`, the poll);
      outside code only `request_page`/`invalidate_key`/`drop_task`/
      `reset`. Peek-then-revalidate (peek lands at once as
      `Revalidating`, a disagreeing revalidate replaces the page),
      bounded pages, and page-extend gating on scroll quiescence
      (`Extending` waits for `pageLayoutReady` before consuming the
      task) are preserved; rendering still reads `fullFileDiff`/
      `fullFilePage`/`sourceWindow`. `fullFileHexPreviewKey` is now
      `optional<PageKey>`; markdown/reading-view identities use
      `page_key_string(key)` (UI only, never load identity). Registry
      `full_file` returns `file_load_name(phase)` (busy is still the
      main task in flight), `full_file_revalidate` stays non-gating.
      Gates: `zig build` clean, `./tests/run_unit_tests.sh
      test_async_slot` 58/58 (3 new: phase names, PageKey equality over
      source/encoding/action/cursor/target + generation rule,
      Loading→Ready→Revalidating→Ready and Extending→Failed lifecycle),
      `test_content_reader` 34/34, `test_source_pages` 5/5,
      `test_loading_feedback` 2/2, `test_diff_tools` 57/57,
      `test_source_find` 8/8 updated for the deleted fields;
      `test_review_target` fails only the pre-existing history-cap
      test noted under Wave 0. `check_unit_registry.py` +
      `check_no_raw_colors.py` + `check_theme_writes.py` ok, flows run
      individually headless passed: `flow_browse_files.e2e`,
      `flow_sticky_scopes.e2e`, `flow_outline_panel.e2e`,
      `flow_blame_gutter.e2e`, `flow_minimap.e2e`,
      `flow_line_numbers_toggle.e2e`, `flow_async_slot_registry.e2e`
      (now validates `async:full_file=Idle`), plus a `/tmp` probe
      quick-opening `src/app.cpp` validating
      `async:full_file=Ready`, and `python3 tests/session_restore.py`
      PASS.
- [x] **M15 hunk context / scope / diff syntax** → slot with
      `Key = renderIdentity`, failures cached as `Failed` instead of
      rendering as absence. `components.h:362-392`, one-in-flight
      discipline in `hunk_context.h:44`. Effort M, risk low.
      Preserve: one-at-a-time + 32/frame budget.
      Done: `HunkContextRuntime.slot` is
      `async_slot::Slot<HunkContextResult, HunkContextKey>` (`identity` +
      renderIdentity-derived `version`), `HunkScopeRuntime.slot` is
      `Slot<OutlineResult>` with a `failed` key set, and per-scope
      `DiffSyntaxRuntime.slot` is `Slot<DiffSyntaxResult, uint64_t>`
      keyed by `renderIdentity` in `src/ecs/components.h`; the deleted
      `future/request/pending/version/identity` fields are gone and
      `Slot::fail()` in `src/util/async_slot.h` lands reader/Git errors
      as `Failed` with the error retained. Poll sites in
      `src/ui/hunk_context.h`, `src/ui/hunk_scope.h` and
      `src/ui/diff_syntax.h` cache failures (hunk-context entries render
      their error notice, failed scope/syntax keys are not relaunched)
      instead of rendering as missing; one-at-a-time and the
      32/frame `resolve_unseeded` budget are unchanged. Registry
      `hunk_context`/`hunk_scopes`/`diff_syntax` report
      `name(slot.phase)` (ready-but-unpolled reads `Ready`, keeping the
      `wait_for_refresh` gate's in-flight semantics). Gates: `zig build`
      clean, `./tests/run_unit_tests.sh test_async_slot` 61/61 (incl.
      new failure-caching tests), `test_diff_tools` 57/57,
      `test_hunk_context` 4/4, `test_content_reader` 34/34,
      `check_unit_registry.py` + `check_no_raw_colors.py` +
      `check_theme_writes.py` ok, flows run individually headless
      passed (0 E2E errors): `flow_async_slot_registry.e2e` (validates
      `async:hunk_scopes=Ready` + `async:diff_syntax=Ready` +
      `async:hunk_context=Idle`), `flow_hunk_scope.e2e`,
      `flow_minimap.e2e`, and `tests/check_diff_context.sh`
      (hunk-context expansion).
- [x] **M13 blame (line + gutter)** → one slot per flavour.
      `components.h:593`, `:600`; gutter has no visible loading state
      today (empty means loading *or* empty). Effort S, risk low.
      Done: `RepoComponent::blame` and `blameGutter` are each
      `async_slot::Slot<git::GitResult>` in `src/ecs/components.h`; the
      deleted `blameFuture/blameFutureStamp/blameError` and
      `blameGutterFuture/blameGutterStamp/blameGutterKey/
      blameGutterError` are replaced by the slots' key/stamp/error.
      `src/ui/source_header.h` requests line blame via `blame.request`,
      `src/ui/full_file_view.h` polls both slots (Git failures land
      `Failed` via `slot.fail(stderr)`), rendering still reads
      `blameLine`/`blameGutterLines`, and the gutter now shows
      `Loading blame` while Pending and `Blame failed` while Failed
      instead of an empty gutter. Registry `blame` (gating,
      in-flight semantics preserved) and `blame_gutter` (non-gating)
      report `name(slot.phase)`. Gates: same build/unit/check evidence
      as M15 (incl. new `blame_slots_poll_fail_and_name_phases` and
      `test_git_parser` 60/60, `test_code_gutter` 2/2), flows run
      individually headless passed (0 E2E errors):
      `flow_blame_gutter.e2e`, `flow_async_slot_registry.e2e`
      (validates `async:blame=Idle` + `async:blame_gutter=Idle`),
      `flow_minimap.e2e`. Note: `tests/check_file_history.sh
      tests/navigation_scripts/improvement_26_blame.e2e` fails at its
      `context_menu_item_Blame line 1` click (menu construction is
      unchanged by this item; the click precedes any blame launch),
      and clean HEAD does not build in this environment to compare,
      so that harness step is recorded unresolved rather than claimed.
- [x] **M7 comparison + range diff** → `Comparison` and `RangeDiff::Step`
      enums replacing the `next` progress counter (`range_diff.h:37`,
      half-filled `resolved[]` survives an abandoned resolution) and the
      derived `comparisonOpen()` predicate with two sources of truth
      (`components.h:657`). Effort M, risk med.
      Done: `ecs::Comparison {Closed, Editing, Loading, Loaded, Failed}` +
      `comparison_name()` and `ecs::RangeDiff` (renamed from
      `RangeDiffState`) with nested `Step {Idle, Resolving, Comparing,
      Done, Failed}` + `range_diff_step_name()` in `src/ecs/components.h`;
      the `next` counter is deleted. `RangeDiff` transitions
      (`begin/begin_resolving/begin_comparing/fail/finish/abandon/reset`)
      are the only step writers in `src/ui/range_diff.h`: a fresh
      submission clears `resolved`, and `abandon()` (panel closed, mode
      toggled, stale result dropped) clears a half-filled `resolved` so
      it never survives into the next open, while a `Done` result and
      its endpoints survive a mode toggle as before. Rendering still
      reads `display`/`error`, and the status line switches on `step`.
      `RepoComponent::comparison` is the stored phase and
      `comparisonOpen()` reads it (`!= Closed`); the editor-modal
      parameter and the workspace-scope derivation are deleted from all
      callers. Transitions: `navigation::comparison_editor` →
      Editing, both compare launches in `src/ui/revision_comparison.h`
      → Loading, `navigation::complete_comparison` → Loaded, git/empty-
      form failures → `fail_comparison` (Failed, error retained),
      `navigation::finish` → `sync_comparison_phase()` (the one
      navigation transition: comparison document Loading while owed/
      in flight or not yet the loaded scope, Loaded when loaded,
      Failed with an error, Closed otherwise), Esc dismiss syncs the
      same way, and both Close buttons now `close_comparison()` +
      close the editor modal explicitly (already on the working-tree
      document, the old `open(wt)` was a no-op and the panel stayed).
      The Document-vs-SubmittedForm distinction is untouched
      (`comparisonRequest`, `comparison_request_key`, and their unit
      tests pass unchanged). Registry entries `comparison`/`range_diff`
      report `comparison_name`/`range_diff_step_name` (busy semantics
      unchanged), so `validate async:comparison` / `async:range_diff`
      name the new phases. Gates: `zig build` clean,
      `./tests/run_unit_tests.sh test_async_slot` 65/65 (4 new:
      comparison names + stored-phase/registry transitions, range-diff
      step names/lifecycle, abandoned-resolution-clears-resolved),
      `test_review_store` 31/31, `test_diff_tools` 57/57;
      `test_review_target` passes all comparison/range tests and fails
      only the pre-existing history-cap test noted under Wave 0.
      `check_unit_registry.py` + `check_no_raw_colors.py` +
      `check_theme_writes.py` ok. Flows run individually headless
      passed: `tests/review_50/item_29.sh` (range diff + revision
      comparison in one script, twice; one `wait_for_refresh` added
      after `full_file_back`, whose reload had no wait), the registry
      flow `flow_async_slot_registry.e2e` (now also validates
      `async:comparison=Closed`), and `/tmp` probes validating
      `async:comparison` Closed→Editing→Esc→Closed and
      `async:range_diff=Done` surviving Close. Note:
      `tests/check_revision_comparison.sh` still fails at its
      Side-by-Side toggle step, the known pre-existing failure at
      HEAD; its compare-load expectations before that step pass.

### Wave 5 — sessions and substrates

- [x] **M11 semantic/LSP** → `Lsp { Unstarted, Starting, Ready, Busy,
      Dead, Failed }` inside `Session::State` (`semantic.cpp:63`), typed
      cancelled flag instead of the `"cancelled"` string compare
      (`semantic_navigation.h:110`), `didClose` on document close so the
      version map stops drifting from disk. Effort M, risk med.
      Preserve: one-request-at-a-time, working-tree-only.
      Done: `semantic::Lsp {Unstarted, Starting, Ready, Busy, Dead,
      Failed}` + `lsp_name()` in `src/lsp/semantic.h`; `Session::State`
      in `src/lsp/semantic.cpp` holds the shared phase, documents as
      `{version, text, server}`, and `State::ensure_client()` is the
      sole transition into `Ready` (Unstarted/Dead/Failed → Starting →
      Ready/Failed, Busy settles back through it to Ready/Dead; a dead
      client drops its documents). One-request-at-a-time (`State::mutex`)
      and working-tree-only (`request_semantic` revision guard) are
      unchanged. `Outcome::cancelled` is a typed bool: `run()`'s ask
      sets it (not `error="cancelled"`), and `poll_semantic` in
      `src/ui/semantic_navigation.h` checks it — the string compare is
      deleted. `Session::close_document(path)` (inline queue in the
      header) + `flush_closed_documents()` send
      `textDocument/didClose` and erase the version entry;
      `navigation::close` queues it when a source document actually
      leaves the open set, `poll_semantic` flushes it next frame, and
      `run()` drains queued closes before/after a Busy request so the
      UI never blocks. Registry `semantic` in `src/ecs/components.h`
      reports `lsp_name(lsp_state())`, so `validate async:semantic`
      names the new phases. Gates: `zig build` clean,
      `./tests/run_unit_tests.sh test_lsp_client` 6/6 (incl. new
      `lsp_name`/typed-cancelled and lifecycle + didClose-reopen
      tests), `test_async_slot` 66/66 (incl. registry
      `async:semantic=Unstarted`), `test_diff_tools` 57/57,
      `test_content_reader`/`test_tree_navigation`/
      `test_document_cycle` passed, `check_unit_registry.py` +
      `check_no_raw_colors.py` + `check_theme_writes.py` ok, flows run
      individually headless passed: `flow_hover_tooltip.e2e` (now
      validates `async:semantic=Ready`), `flow_async_slot_registry.e2e`
      (now validates `async:semantic=Unstarted`),
      `flow_declaration_ranking.e2e`.
- [x] **M10 repository search** → `Search { Closed, Debouncing, Running,
      Done, Failed, External }`; the `External` state retires the
      semantic-injection hack that fakes `ObservedText` to stop the panel
      re-searching (`semantic_navigation.h:84`), and `Failed` becomes
      distinct from "no matches" (today a 4-way ternary,
      `repo_search.h:349`). Effort L, risk med. Preserve batch streaming,
      excerpt cache.
      Done: `ecs::Search {Closed, Debouncing, Running, Done, Failed,
      External}` + `search_name()` in `src/ecs/components.h`;
      `RepoComponent::repoSearchPhase` replaces the deleted
      `repoSearchOpen` bool and `repoSearchObservedText` array (all
      readers now use `repo_search_open()`). The transitions
      `open_search/close_search/debounce_search/begin_search/
      finish_search/fail_search/show_search_external` are the only phase
      writers; the submitted triple (query on `repoSearchSubmittedQuery`,
      include/exclude globs on `repoSearchScope`) is recorded by
      `note_search_submitted()` inside those transitions, so semantic
      injection (`src/ui/semantic_navigation.h`) lands in `External`
      via `show_search_external()` with no fake observed text, and
      editing query/globs leaves External for Debouncing. The status
      line in `src/ui/repo_search.h` is an exhaustive switch on the
      phase: `Failed` renders the retained error (distinct from Done's
      "No matches"), External renders `repoSearchSource`; batch
      streaming (`repoSearchBatches`), the excerpt cache, and
      Shift+Enter expansion are untouched. Registry `repo_search` now
      reports `search_name(repoSearchPhase)` (busy in Debouncing/
      Running), so `validate async:repo_search` names every phase.
      Gates: `zig build` clean, `./tests/run_unit_tests.sh
      test_async_slot` 69/69 (3 new: name coverage, phase/registry
      lifecycle incl. Failed-distinct and async_idle, External not
      mistaken for a text edit), `test_repository_search` 14/14,
      `check_unit_registry.py` + `check_no_raw_colors.py` +
      `check_theme_writes.py` ok, flows run individually headless
      passed: `flow_search_excerpt.e2e` (excerpt cache + Shift+Enter),
      `improvement_22_repo_search.e2e` (now validates
      `async:repo_search=Done`), `flow_search_identifier.e2e`,
      `flow_declaration_ranking.e2e`, `flow_async_slot_registry.e2e`
      (now validates `async:repo_search=Closed`), and the new
      `flow_search_failed.e2e` (invalid regex lands in
      `async:repo_search=Failed`, no "No matches"). Note: a /tmp probe
      driving Find References to `External` via clangd did not reach
      the panel in this environment (references returned no panel),
      so External is covered by the unit tests above, not a flow.
- [x] **M25 source edit buffer** → `Edit { Viewing, Seeding,
      EditingClean, EditingDirty, Saving, SaveFailed }` on the document
      (`reading_workspace.h:243`); seed failure currently exits edit mode
      silently, and one save path clears dirty unconditionally
      (`source_edit.h:70`). Effort M, risk med (data-loss adjacent).
      Preserve CRLF/trailing-newline fidelity.
      Done: `reading::Edit` + `edit_name()` and the transition methods
      (`begin_edit/stop_edit/finish_edit_seed/fail_edit_seed/
      begin_edit_save/finish_edit_save/fail_edit_save/mark_edit_dirty`,
      with `edit_mode()/edit_seeded()/edit_dirty()` derived) on
      `reading::Document` in `src/util/reading_workspace.h` replace the
      deleted `editMode/editSeeded/editDirty` bools. `seed_edit_buffer`
      in `src/ui/source_edit.h` lands Seeding→EditingClean (a retained
      buffer resumes as EditingDirty exactly when it differs from
      disk) or →Viewing with `editError` retained, which
      `render_full_file` surfaces as an error toast instead of silently
      exiting; `save_edit_buffer` goes Saving→EditingClean only on a
      successful write and →SaveFailed (dirty retained, header pill
      shows "Save failed", toast on failure) otherwise — dirty is
      never cleared by a failed save. CRLF/trailing-newline parsing
      and `edit_buffer_text` reconstruction are unchanged; all readers
      (header pill/menu, vim gate, semantic text, tab titles, main
      content hash, `edit_mode`/`edit_dirty` validate keys) read the
      derived helpers. `validate async:edit` in `src/main.cpp` returns
      `edit_name(edit)`. Gates: `zig build` clean,
      `./tests/run_unit_tests.sh test_async_slot` 72/72 (2 new: name
      coverage, phase/dirty lifecycle incl. seed-failure and
      SaveFailed-retains-dirty) plus `test_diff_tools`,
      `test_document_titles`, `test_document_cycle` passed,
      `check_unit_registry.py` + `check_no_raw_colors.py` +
      `check_theme_writes.py` ok, flows run individually headless
      passed: `flow_edit_file.e2e` (now validates
      `async:edit` Viewing→EditingClean→EditingDirty→EditingClean→
      Viewing), `flow_file_open_editor.e2e`,
      `flow_async_slot_registry.e2e` (validates `async:edit=Viewing`),
      plus `/tmp` probes: seed failure on an oversized file toasts
      "Couldn't open" and stays Viewing, and stop/re-enter resumes a
      dirty retained buffer as EditingDirty.
- [x] **M20 file watcher** → `Watch { Idle, Swapping, Watching,
      CoolingDown }`, scope as `optional<Scope>` (today a bool+scope pair
      where scope-without-pending is representable). Already the most
      disciplined of the set (`file_watcher_system.h:99`).
      Effort S, risk low.
      Done: `ecs::Watch` + `watch_name()` in `src/ecs/components.h`;
      `FileWatcherSystem::phase()` in `src/ecs/file_watcher_system.h`
      derives Swapping (swap in flight) / Idle (nothing watched) /
      CoolingDown (a scope is pending) / Watching from the swap task,
      watched path and the new `std::optional<refresh_scope::Scope>
      pendingScope_`, which replaces the deleted `pending_` bool +
      `pending_scope_` pair (coalescing merges into the held scope,
      drain-every-frame and the refresh-busy/cooldown handling are
      unchanged). `validate async:watch` in `src/main.cpp` returns
      `watch_name(phase())`. Gates: same build/unit/check evidence
      as M25 (incl. new `watch_name_covers_all_phases` + optional
      coalescing test), flows run individually headless passed:
      `flow_file_watcher.e2e` (now validates
      `async:watch=Watching`), `flow_async_slot_registry.e2e`
      (validates `async:watch=Idle`).
- [x] **M26 executor** → do not rewrite. Optionally add a `Task::rejected`
      flag in `launch_on` (`async_task.h:151`) so "queue full" stops
      masquerading as an empty result via sentinel error strings.
      Effort S, risk low.
      Done: `Task::rejected()` in `src/util/async_task.h` (flag stored on
      the task, moved with it, false for accepted/`completed()`/empty
      tasks); `launch_on` sets it exactly when `Executor::submit` refuses
      (queue full or shutting down) and still delivers the caller-supplied
      rejected value, so owners keep working and sentinel strings may stay
      as messages. Gates: `zig build` clean,
      `./tests/run_unit_tests.sh test_async_task` 4/4 (new
      `rejected_flag_marks_queue_full_and_shutdown_only`: background +
      foreground queue-full rejected, accepted/completed/empty not,
      move preserves, post-shutdown rejected).
- [ ] **Minor machines, opportunistic (S/low each):** M27 focus
      pending/reveal (name phases in tests only); M29 selected-diff /
      codeowners / selection-copy / markdown-link / outline-panel
      `future + key` pairs → slot instantiations, no bespoke work;
      M31 layout shelf 3-bool → `Collapsed|Expanded|Animating` if touched.
      Partially done (M26+minors pass): M29 selection-copy and
      markdown-link are now `async_slot::Slot` instantiations
      (`SelectionCopyRuntime::slot`, `MarkdownLinkRuntime::slot` in
      `src/ecs/components.h`; the `request`/`future` fields are deleted,
      poll sites in `src/ui/diff_renderer.h`/`src/ui/full_file_view.h`
      call `slot.poll(repo)`, registry entries `selection_copy`/
      `markdown_link` report slot phases, frame pacer reads `slot.task`).
      Gates: `zig build` clean, `test_async_slot` 73/73 (incl. new
      migrated-slot poll/phase test), `test_async_task` 4/4,
      `test_diff_tools` 57/57, `test_selection_copy` 9/9,
      `check_unit_registry.py` + `check_no_raw_colors.py` +
      `check_theme_writes.py` ok, flows run headless passed 3/3
      (`flow_markdown_links`, `flow_async_slot_registry`,
      `flow_copy_snippet` from a `/tmp` copies dir). Left undone as not
      trivially safe in this pass: codeowners (two live keys —
      current vs in-flight — plus an 8-entry cache fight over one
      future), outline panel (picker `symbols`/`projectSymbols` +
      source-position `future+key` intertwined with the M8 shell),
      M31 shelf (derived `shelfCollapsed` + transition latch +
      `optional` user override driving native window resize; no
      Animating state exists to name), M27 (tests-only naming, no
      production change to preserve). M23/M30/M32 untouched per above.

## Explicitly not changing

- **M23 reading workspace core** — already explicit; at most split
  `generation_` (activation clock) from a document-version counter, and
  only if a bug demands it. Risk med, value low.
- **M30 tab strip** — the `ActiveTab` ECS marker *is* the state; only the
  away-threshold refresh rule deserves a named constant + test.
- **M32 commit prefetch** — the exemplar. No change.
- **Rejected as not state machines:** `FileDiff`/`FileStatus` parsed git
  data (`components.h:126`, `:69`); LRU caches (`blob_page_cache.h:31` —
  key invalidation is their job); toast lifecycle (owned upstream);
  `CommitReviewQueue` cursor (`components.h:103`, pure transition);
  `frame_pacer::decide` (pure function); already-named enums
  (`ReviewVerdict`, `image_view_state::Mode`, `HistoryScope::Mode`,
  `DiffTarget::Kind`).

## Per-item definition of done

Build clean; the machine's existing unit suites pass; flow suite passes;
for any user-visible state change (new `Failed` rendering, cancel
affordance), a flow script or screenshot shows the before/after; the
removed bools/keys are actually deleted, not wrapped-and-kept; and the
`validate async:<name>` key (Wave 0) names the new phases in a test.
