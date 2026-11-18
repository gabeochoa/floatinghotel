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

- [ ] Add `src/util/async_slot.h` per the decision above, with a unit
      test (`test_async_slot`: request→poll ready, cancel→Idle, stale
      stamp rejected, failed task→Failed with error retained).
      Register it in `tests/run_unit_tests.sh` only.
- [ ] Add the per-`RepoComponent` slot registry + `validate async:<name>`
      key; switch the `main.cpp:750` idle gate to iterate the registry
      (gate behaviour identical: same set of machines, now self-describing).

### Wave 1 — prove it on clones and stuck-state risks

- [ ] **M14 history / commit-search / review-queue panels** → one
      `AsyncSlot` instantiation each. Lives: `components.h:585`
      (file history), `:605` (commit search), `:625` (review queue);
      triplicated `Closed → Open → Loading → List|Error` with poll sites
      at `file_history.h:11`, `commit_search.h:16`,
      `revision_comparison.h:88`. Deletes the most duplicated poll code.
      Effort M (as one refactor), risk low. Preserve: entry types stay distinct.
- [ ] **M21 network ops** → per-op phase + elapsed + cancel.
      `components.h:1466` `PendingNetworkOp`, poll
      `network_ops_system.h:47`. Today fire-and-forget: a hung push is
      indistinguishable from a slow one. Effort S, risk low.
      Preserve: toast-on-complete, refresh only if repo still matches.
- [ ] **M2 commit-log paging + history scope** → `LogPhase` on
      `CommitLogPage` (`components.h:293`, `:441`); `hasMore` computed
      once from returned count vs page size instead of `size == 100`
      inferred in three places (`async_git_refresh_system.h:316,432,445`).
      Effort S, risk low. Preserve: 100-page size, HEAD pinning (`:459`).
- [ ] **M22 settings save** → `Save` enum + one `apply_write_result()`.
      `settings.h:126` (`pendingSave_`, `writeFuture_`, latch
      `loadError`), `settings.cpp:248`. Blocked saves are currently
      silent in the UI. Effort S, risk low. Preserve: 250 ms debounce,
      never-overwrite-on-load-error.

### Wave 2 — the money and data-loss surfaces

- [ ] **M6 commit-detail patch** → `AsyncSlot<CommitPatch, PatchKey>`
      with `PatchKey{repo, commit, parent, context, ignoreWs}` as a
      struct (identity is currently 5 hand-compared fields,
      `components.h:682`, `commit_detail.h:85`, duplicated in
      `navigation.h:238`). Effort M, risk med. Preserve peek/revalidate.
- [ ] **M17 review session store** → `ReviewStore` phases incl.
      `SaveFailed` surfaced in the basket header. `components.h:732`
      (`reviewing`, `dirty`, `nextSaveAttempt` retry loop at `:769`),
      `review_store.cpp:122` (`loadError` blocks all future saves with no
      recovery transition). The only machine where an implicit state can
      lose user work silently. Effort M, risk med.
      Preserve: JSON schema v2, scope keying.
- [ ] **M16 commit editor flow** → `CommitFlow` enum; replace the
      one-frame `commitRequested` flag (consumed in 4 places,
      `sidebar_system.h:190`) with `request_commit()`. `components.h:1377`.
      Effort S, risk low-med — test well, it's the write path.
      Preserve: split-button one-shot policy override.
- [ ] **M19 review baseline** → `Baseline` enum, snapshot as
      `optional<string>`. `components.h:779`; `snapshotCapturing`
      duplicates `snapshotFuture.valid()` and can disagree with it.
      Effort S, risk low.
- [ ] **M18 comment composer** → `Composer` enum with
      `variant<HunkKey, CommentIndex>` target. `components.h:748`
      (12 composing fields) + `:1070` transition trio (keep them as the
      sole mutators). Effort S, risk low. Preserve draft-on-dismiss.

### Wave 3 — the modal archetype and its modes

- [ ] **M8 file picker + M9 picker modes (together)** → `Picker` phase
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
- [ ] **M28 repo picker** → fold into the M8 shell (own field set today:
      `components.h:1281` plus the welcome `repositoryPicker*` at `:1275`).
      Effort M, risk low. Depends on M8.
- [ ] **M24 modal constellation** → promote `focus::Popup` from a derived
      view to stored state: `LayoutComponent::modal: optional<Popup>` +
      per-dialog payloads, `close_top()` for Esc, `open_popups()` becomes
      a projection then is deleted. Writers today set ~15 open-bools
      independently (`components.h:399` push dialog, `:1158` branch
      dialogs, `:1288` relink, `:1340` shortcuts/options), so two modals
      can be "open" with precedence decided by read ordering — the
      highest stuck-UX risk in the app (a leftover bool leaves an input
      gate installed). Dialog by dialog, after M8. Effort L, risk med.
      Preserve: focus-return stack (`focus_target.h:47`).
- [ ] **M12 find-in-file** → `Find { Closed, Editing, Scanning }`.
      `reading_workspace.h:197` (three bools) + source-scan runtime
      `components.h:346`; diff find (sync) and source find (async) share
      UI with divergent close paths. Effort S, risk low.

### Wave 4 — refresh core and the big readers

- [ ] **M1 async git refresh** → `RefreshPhase { NeverLoaded, Queued,
      Running, Settled }` over the existing per-read sub-state
      (`PendingFutures`, `async_git_refresh_system.h:488`). Completion is
      currently "all optionals empty" (`:396`); `commitLogLoading` is set
      from three places and can clear early. Effort M, risk med — every
      panel depends on it, so it comes after its feeders are slotted.
      Preserve: prefetch adoption (`:150`), scope→plan mapping,
      `dataGeneration` bump only on status change (`:292`).
- [ ] **M4 untracked review** → named sub-read of M1 (or a slot): the
      `optional<unsigned>` generation-as-dirty-flag (`components.h:460`)
      with three reset writers is the least discoverable idiom in the
      file. Effort S, risk low. Preserve append-dedupe by path.
- [ ] **M3 selected-file diff** → `DiffSource { Whole, SinglePending,
      Single, Failed }` replacing the `~0u` sentinel generation compare
      (`components.h:474`, `main_content_system.h:1135`); a failed single
      diff currently leaves the previous diff shown silently.
      Effort S, risk low.
- [ ] **M5 full-file paged reading** → `FileLoad` enum + `PageKey`
      struct replacing both concatenated cache-key strings (built in two
      places, `full_file_view.h:127` vs `:183` — drift means infinite
      reload or a stale page; the "Source changed while scrolling" error
      is the visible symptom). Largest machine in the app
      (`components.h:481-530`, `:584`). Effort L, risk high — hottest
      render path, goes late and behind its own flow tests.
      Preserve: peek-then-revalidate, bounded pages, spinner delay.
- [ ] **M15 hunk context / scope / diff syntax** → slot with
      `Key = renderIdentity`, failures cached as `Failed` instead of
      rendering as absence. `components.h:362-392`, one-in-flight
      discipline in `hunk_context.h:44`. Effort M, risk low.
      Preserve: one-at-a-time + 32/frame budget.
- [ ] **M13 blame (line + gutter)** → one slot per flavour.
      `components.h:593`, `:600`; gutter has no visible loading state
      today (empty means loading *or* empty). Effort S, risk low.
- [ ] **M7 comparison + range diff** → `Comparison` and `RangeDiff::Step`
      enums replacing the `next` progress counter (`range_diff.h:37`,
      half-filled `resolved[]` survives an abandoned resolution) and the
      derived `comparisonOpen()` predicate with two sources of truth
      (`components.h:657`). Effort M, risk med.

### Wave 5 — sessions and substrates

- [ ] **M11 semantic/LSP** → `Lsp { Unstarted, Starting, Ready, Busy,
      Dead, Failed }` inside `Session::State` (`semantic.cpp:63`), typed
      cancelled flag instead of the `"cancelled"` string compare
      (`semantic_navigation.h:110`), `didClose` on document close so the
      version map stops drifting from disk. Effort M, risk med.
      Preserve: one-request-at-a-time, working-tree-only.
- [ ] **M10 repository search** → `Search { Closed, Debouncing, Running,
      Done, Failed, External }`; the `External` state retires the
      semantic-injection hack that fakes `ObservedText` to stop the panel
      re-searching (`semantic_navigation.h:84`), and `Failed` becomes
      distinct from "no matches" (today a 4-way ternary,
      `repo_search.h:349`). Effort L, risk med. Preserve batch streaming,
      excerpt cache.
- [ ] **M25 source edit buffer** → `Edit { Viewing, Seeding,
      EditingClean, EditingDirty, Saving, SaveFailed }` on the document
      (`reading_workspace.h:243`); seed failure currently exits edit mode
      silently, and one save path clears dirty unconditionally
      (`source_edit.h:70`). Effort M, risk med (data-loss adjacent).
      Preserve CRLF/trailing-newline fidelity.
- [ ] **M20 file watcher** → `Watch { Idle, Swapping, Watching,
      CoolingDown }`, scope as `optional<Scope>` (today a bool+scope pair
      where scope-without-pending is representable). Already the most
      disciplined of the set (`file_watcher_system.h:99`).
      Effort S, risk low.
- [ ] **M26 executor** → do not rewrite. Optionally add a `Task::rejected`
      flag in `launch_on` (`async_task.h:151`) so "queue full" stops
      masquerading as an empty result via sentinel error strings.
      Effort S, risk low.
- [ ] **Minor machines, opportunistic (S/low each):** M27 focus
      pending/reveal (name phases in tests only); M29 selected-diff /
      codeowners / selection-copy / markdown-link / outline-panel
      `future + key` pairs → slot instantiations, no bespoke work;
      M31 layout shelf 3-bool → `Collapsed|Expanded|Animating` if touched.

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
