# floatinghotel triage

Open work and recorded decisions, originally reconciled against source at `1a2fcf3` on September 13, 2026.
The [60-step reading plan](docs/plans/2026-09-12-reading-navigation-polish.md) and
[final replay](docs/reading-navigation-step60.md) record completed work and evidence.
Older visual specifications do not override the current reading workflow.

Keep changes above history, single-line commit rows without authors, larger code
text independent of UI size, wrapping, a fixed footer, quiet startup, direct dock
resizing, and independent staged/unstaged review. Review feedback and marking
viewed must never stage code implicitly. Escape dismisses temporary UI first.

## px0 follow-up

Added September 13, 2026, from px0 commit
[`5743e84`](https://github.com/px0-ai/px0/commit/5743e84e6bc4dd2e309200206f957ed540df6976),
version 0.1.1. These tasks extend the completed
[reading/navigation plan](docs/plans/2026-09-12-reading-navigation-polish.md).
Keep code read-only and preserve revision identity, review origins, and bounded
caches. No px0 application or benchmark has run yet.

### P0: reduce reading latency

- [x] Instrument dispatch, queue wait, Git validation, cache lookup, decoding,
      publication, layout, and matching-frame completion separately. Use the
      [step 60 measurements](docs/reading-navigation-step60.md) as the baseline.
      Verified by six reading replays; [evidence](docs/reading-backlog.md#reading-runtime-and-measurements).
- [x] Reduce repeated Git metadata processes before historical-source cache hits.
      Measure replacing `rev-parse`, `cat-file -s`, and mode lookup with one
      bounded `ls-tree -l -z` lookup of the resolved revision and literal path.
      Missing objects, shallow history, unusual paths, symlinks, gitlinks, index,
      and working-tree freshness pass 32 content-reader tests. In 120 warm reads
      per variant, p95 fell from 203.36 to 80.18 ms; full-switch targets remain open.
- [x] Defer split-diff `changed_ranges` until a pair has a visible fragment.
      Count calls and time `render_sbs_hunk` before and after; require identical
      clipboard bytes, wrapping, row identities, and geometry.
- [x] Cache split-row pairing and cumulative wrapped heights so offscreen rows
      skip preparation. Include content identity, hunk, width, font, and whitespace
      mode in the key. Resize, zoom, folding, local context, asymmetric
      additions/deletions, and content replacement pass within existing budgets.
      Settled frames prepare zero pairs; 23–76 emitted pairs require comparison
      in the 3,000-pair fixture. [Evidence](docs/reading-backlog.md#reading-runtime-and-measurements).
- [x] Profile repeated whole-hunk review hashes and wrap-cache vector copies on
      unchanged frames. Remove measured duplicate work and verify invalidation
      when source contents or review state changes. Metrics tests and split replay
      pass; persistent review keys are unchanged. Final-verdict signature hashing
      remains a separate profiling candidate.
- [ ] Improve warm tab switching within the one-active-runtime design. Measure
      immutable descriptor reuse and cache-hit copying before retaining more data.
      Keep inactive tabs lightweight; do not copy px0's larger cache budget without
      memory measurements. Preserve unavailable-revision behavior.
- [ ] Meet p95 selection feedback <=50 ms, warm correct-content switching <=100 ms,
      and CPU frame p99 <=20 ms. Retain failures and variability in the report.

### P0: benchmark the same user journeys against px0

- [ ] Build a repeatable comparison runner with pinned optimized app versions,
      fixture commits, browser version, machine, OS, display scale, and font size.
      Use a dedicated browser profile and disposable repositories. Run the apps
      sequentially with `nice -n 10`; disable px0 LSP for the shared reading scope.
- [ ] Cover tree-open, Quick Open, tab A/B/A, Back/Forward, Find on a later page,
      repository search to a line, scrolling, selection/copy, and Markdown preview.
      Run at 100%, 140%, and 200% zoom plus a narrow window. Measure correct visible
      content, not HTTP completion; record CPU completion and presentation separately.
- [ ] Use a deterministic small repository, pinned Flask/Redis/React/TypeScript
      snapshots, a 400,000-line file below 64 MiB, long lines, Unicode, CRLF, and
      Markdown links. Match indexed files, searched bytes, exclusions, regex
      behavior, and result limits before comparing search speed.
- [ ] Separate fresh processes with warm OS caches, cold-storage runs where
      practical, existing-tab switching, cached reads, actual close/reopen, and
      eviction. px0 closes evict backend content, so repeated HTTP GETs do not
      represent reopening a closed tab.
- [ ] Collect at least 30 launches, 100 repetitions per warm journey, and 1,000
      frames for frame p99 across alternating app blocks. Publish p50, p95, maximum,
      uncertainty, timeouts, and failures for every predeclared journey.
- [ ] Sample resident memory continuously through 20 tabs, scrolling, closure, and
      30 seconds idle. Count px0's server, browser, renderer, utility, and GPU
      processes; report components and totals. Keep owned-cache bytes separate.
- [ ] Stress rapid A/B/A opens, superseded searches, closing during loading,
      changed files between pages, same-size writes with preserved mtime,
      multiline syntax on distant pages, and eviction. Assert revisions, search
      matches, copied bytes, and cancellation. Compare first text and correct
      syntax readiness separately; stale or blank content is not a successful load.
- [ ] Demonstrate at least 20% lower p95 on a predeclared shared journey, with
      uncertainty excluding parity and no correctness regression. Publish all
      journeys and memory costs before claiming a performance win.

### P1: navigation without a language server

- [ ] Add a document outline and searchable symbol picker through Quick Open.
      Scan supported C-family, Python, JavaScript, TypeScript, and Markdown forms
      asynchronously, keyed by content identity. Label approximate results,
      suppress comments/strings, and verify multiline declarations, duplicates,
      cancellation, historical revisions, and symbols beyond the first page.
- [ ] Highlight occurrences of the selected identifier without replacing Find's
      query or the reading selection. Use decoded word boundaries; test Unicode,
      comments, shadowed names, historical content, and stale results.
- [ ] Add "Search this identifier" from the selection or caret to the retained
      search pane. Use whole-word matching and the active revision; label results
      as textual matches rather than semantic references.
- [ ] Add approximate declaration ranking for textual navigation. Keep it visibly
      distinct from semantic go-to-definition and test ambiguous or duplicate names.
- [ ] Make Markdown relative links and heading anchors navigate through typed
      revision-aware destinations. Preserve source/preview position and Back/Forward.

### P2: reader completeness

- [ ] Extend Markdown preview with tables, inline formatting, and bounded local
      images. Test links and image paths in historical revisions as well as the
      working tree.
- [ ] Add a searchable command palette backed by existing commands and shortcut
      ownership. Restore focus and the reading anchor on dismissal.
- [ ] Add source-snippet copy with a revision-aware location and fenced code for
      agent prompts. Preserve plain copy and existing copy-size limits.
- [ ] Add a line-number visibility toggle without moving the logical reading anchor.
- [ ] Expand syntax-language support based on real repositories. Define a data-driven
      extension/language configuration where it avoids code changes for new mappings.
      Verify multiline state, distant pages, and independent before/after diff states.
- [ ] Complete the light theme and then add theme choices. Check code, diff colors,
      hover, caret, selection, and Find highlights at every supported zoom.
- [ ] Check image-preview format parity with px0 using shared fixtures. Record
      unsupported formats and add the useful missing ones within decoding limits.

### Later: semantic navigation

These capabilities remain outside the original 60-step scope. Track them as
separate read-only work; editor mutation features are not required for parity.

- [ ] Add semantic go-to-definition, references, and type hover with exact
      destination identity and cancellation when the active document changes.
- [ ] Add call hierarchy/trails with Back/Forward and explicit handling for stale
      or unavailable server results.
- [ ] Design language-server discovery and installation separately. Keep outline,
      occurrences, textual search, and Markdown navigation usable without a server.

### Evidence and benchmark setup

The [published px0 benchmark](https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/benchmark.sh)
uses fastest-of-five HTTP requests and Linux server RSS, excluding the browser.
Its end-of-run RSS sample is not a measured peak. Do not compare those numbers
directly with floatinghotel's input-to-frame measurements.

px0 retains browser chunks and budgets 512 MiB in its backend cache. First open
reads the whole file, capped at 64 MiB. Full-file syntax refinement runs only up
to 2 MiB. These tradeoffs belong in the comparison. On macOS, `/api/metrics` falls
back to Go memory accounting rather than RSS; use native process measurements.
Its 2.5-second UI polling may also prevent the server's 15-second idle cleanup.
Verify that behavior with the browser open.

One recorded warm historical-source read spent about 355 ms in three metadata
commands before a cache hit and reached ready at 358.20 ms. Large split diffs also
spent about 10 ms per frame in main-content construction despite only hundreds
of mounted entities. These observations identify experiments, not proven fixes.
The final selection repeat passed at 3.10 to 15.41 ms p99. Original misses and
source/commit latency distributions remain in the
[step 60 evidence](docs/reading-navigation-evidence/step60/acceptance-summary.json).

For a reproducible px0 run, use the pinned commit above with Go 1.25 or newer and
Node, or the official
[darwin-arm64 v0.1.1 binary](https://github.com/px0-ai/px0/releases/download/v0.1.1/px0-0.1.1-darwin-arm64).
Verify SHA-256 `7d075e6e15c37d00596f78487acd218942a0f62b0724a2fad32cd8eaddefcfa1`
before execution. Launch with `-no-open -no-lsp -quiet -host 127.0.0.1` and an
isolated fixture. Confirm `/api/meta` readiness and the expected root/version.
A dedicated Chrome profile and CDP can drive `#tabs .tab.active`,
`#rows .row[data-l] .c`, `#viewport`, `#q`, `#results`, and `#mdview` without changing
px0 source. Preserve traces and close only the test processes.


## Refactoring research

Unchecked items are source-backed candidates; completed items link to verification.
Do the small changes first and keep existing ownership and memory limits. The
px0 performance tasks above cover offscreen diff preparation and review hashes.

- [x] Use the existing atomic-write helper for settings. Completed by CP-095; atomic writes and failure preservation verified in settings tests.

- [x] Coalesce settings saves and skip unchanged values. Setters call
      `save_if_auto`, which serializes the entire settings object. Measure writes
      during resizing, font changes, and navigation; flush the final value at
      shutdown and verify restart behavior. Keep defaults and migrations in a
      typed settings value instead of adding an untyped global key-value registry.
      Implemented a 250 ms pause: 60 resize/font/navigation updates produce one
      write; unchanged updates produce none. Immediate shutdown and failed-relink
      recovery pass. [Evidence](docs/reading-backlog.md#settings-and-panel-size).
- [x] Make active and recent document access explicit. `ReadingWorkspace::source`
      in [reading_workspace.h](src/util/reading_workspace.h) falls back to a recent
      source while a review is active. `set_caret` and `toggle_source_fold` in
      [navigation.h](src/util/navigation.h) use it as a source check. Replace that
      ambiguous accessor with named active/recent queries, migrate callers, and
      remove the old API. Verified by navigation unit tests, reading journeys,
      logical-anchor restoration, and source-folding replay.
- [x] Remove temporary allocation from request validation. `navigation::accepts`
      constructs a complete stamp, copying repository, location, and key merely
      to compare them. Compare borrowed current fields directly while preserving
      repository, document, key, generation, and data-generation checks. Measure
      allocation counts and rerun delayed/out-of-order publication tests.
      10,000 validations allocate zero times; navigation/publication tests pass.
- [x] Avoid copying cached wrap vectors on every hit. `DiffMetricsCache::wraps`
      and `source_rows` in [diff_metrics.h](src/ui/diff_metrics.h) return vectors
      by value, even on a hit. Measure the cost, then use an explicit lifetime-safe
      view or immutable owner. A later cache insertion can evict an earlier entry;
      test eviction while composing both sides, and keep all cache bytes accounted.
      Shared leases survive eviction, pinned bytes constrain admission, and
      wrapping/anchor/source-folding replays pass within the existing budget.
- [ ] Give snapshot reading an explicit content owner. The snapshot renderer in
      [review_snapshot.h](src/ui/review_snapshot.h) disables the normal reader
      binding to avoid borrowing another document's state. Introduce a typed
      reading context for source, review, and saved snapshots, keeping selection,
      Find, anchors, and copy attached to the correct content. Then enable snapshot
      character selection with identity and bounded-copy tests. Avoid a second viewer.
- [x] Replace implementation-defined persistent review keys with a stable encoding. Completed by CP-096; SHA-256 keys and legacy migration verified in review-store tests.

- [ ] Share the repeated Quick Open test setup. Several final-replay corrections
      required waiting for historical paths, choosing scope, and clearing retained
      input. Extract a small helper used by those journeys with explicit scope and
      keep/preview behavior. Retain each journey's independent destination assertions;
      do not introduce a new test language or hide readiness failures.

## Remaining review and reader work

- [x] Pin the actual file header while scrolling, retaining its fold control, path,
      status, and actions. The original header and button IDs stay in use. Pinned
      folding, Viewed, source return, the next file, and narrow windows pass at
      100%, 140%, and 200%. Find, caret, selection, and logical anchors remain below
      the pinned header; see [verification](docs/reading-backlog.md#pinned-file-headers).

- [ ] Split large changes into independently reviewable chunks of about 20 lines.
      Preserve original hunk identities and generate valid patches at safe seams.
      Whole-hunk and selected-line staging already exist; verify partial edits,
      adjacent changes, missing final newlines, and rejected patches.
- [ ] Carry a working-tree comment to the commit that contains its reviewed change.
      Match saved content rather than line number alone, preserve ambiguity warnings,
      and keep exported commit/file/line identities correct across later edits.
- [ ] Add a richer submodule view showing the old/new commit range and a route into
      the submodule repository. Detection, gitlink diffs, and pointer staging already
      exist; handle missing checkouts and unavailable objects explicitly.
- [ ] Reduce redundant visible Copy/Copy Diff buttons where selection and keyboard
      copy provide the same action. Keep discoverable, keyboard-accessible menu actions
      for plain code, diff, path, and location; preserve bulk diff export.
- [ ] Add enclosing function/class context to diff navigation using the planned
      document-symbol scan. Preserve revision and side and label approximate scopes.
- [x] Persist the command-log height. The saved value uses logical pixels and
      survives temporary viewport clamping. Dragging, restart, dock transitions,
      and narrow windows pass at 100%, 140%, and 200%.
      [Evidence](docs/reading-backlog.md#settings-and-panel-size).
- [ ] Finish logical-pixel typography in remaining legacy Git controls. Reproduce
      the outstanding legacy zoom/Options overflow warnings before changing layout;
      keep screenshots and geometry assertions at 100%, 140%, and 200%.
- [ ] Verify native menu tracking and shortcut delivery in a normal macOS session,
      plus physical live resizing with presentation timing. Hidden native probes
      and headless screenshots do not establish compositor behavior.
- [ ] Revisit the full enclosing diff-card border against the approved mock, without
      restoring excessive header height or changing code/selection geometry.
- [ ] Make large repository tabs economical as well as document tabs. Measure
      inactive repository payloads and duplicate opens before considering shared
      immutable data or opt-in background refresh. Preserve separate reading state.

## Afterhours work

Detailed bugs, reproductions, and workarounds stay in the
[upstream-gap ledger](docs/afterhours-gaps.md). Do not edit the vendor checkout.

- [ ] Finish atomic-save durability and concurrent-writer semantics upstream.
      The helper now exists; it still uses a fixed sibling temp name and does not
      fsync file/directory data. Test interruption, disk-full, rename failure, and
      competing writers before claiming power-loss durability.
- [ ] Add CJK/emoji fallback and a reusable styled-text path whose measurement,
      ellipsis, wrapping, selection, and drawing agree at every zoom.
- [ ] Upstream reusable semantic focus return, shortcut/input consumption, logical
      scroll anchors, variable-height virtualization, and virtualized text selection
      from the proven app workarounds. Keep repository identity as app policy.
- [ ] Upstream bounded background work, observable byte-budget caches, file-change
      subscriptions, CPU image decoding/render-thread upload, and delayed busy
      feedback where the current framework still lacks the required contract.
- [ ] Improve native/headless test support: matched layout/presentation barriers,
      isolated config/save/temp paths, clipped-visibility queries, Unicode input,
      clipboard isolation, native resize driving, and zoom-consistent coordinates.
- [ ] Reproduce the remaining historical nested-scroll and invalid-autolayout-mapping
      concerns against the pinned vendor revision. Retire resolved reports;
      retain only confirmed failures and their smallest reproductions in the ledger.

## Later product work

These are optional extensions, not requirements for the current read-only reader.
Existing explicit Git actions remain separate from review feedback.

- [ ] Add commit-message templates and conventional-commit prefixes if the commit
      editor remains part of the intended workflow. Preserve subject/body formatting.
- [ ] Verify cancellation and progress for long network Git operations, plus
      force-push and destructive-operation confirmation. Use disposable local remotes.
- [ ] Design optional review synchronization through Git notes or dedicated refs,
      including concurrent edits and conflict resolution. Keep local review files usable.
- [ ] Explore a history scrubber that highlights newly arrived changes while keeping
      precise revision/line navigation and the saved review baseline distinct.
- [ ] Define supported release platforms and packaging, then verify installation,
      resource lookup, multi-monitor/DPI behavior, accessibility, and upgrade migration.
      The `fh` CLI installer already exists in [install.sh](scripts/install.sh).
- [ ] Evaluate an integrated terminal/agent bridge separately: PTY lifecycle, terminal
      parser choice, Unicode selection, scrollback, and explicit send actions.
- [ ] Reassess broader Git-management requests individually: interactive rebase,
      merge conflict resolution, worktrees, richer tags/remotes/stash management,
      Git LFS, hooks, repository publishing, and hosted PR/CI integration. Existing
      branch, staging, commit, push/pull/fetch, and basic stash actions are not new work.
- [ ] Reassess editing features separately if the product scope changes: completion,
      safe rename, multi-cursor/block selection, and project-wide replacement.

## Commit Plus ideas to prioritize

Added September 19, 2026. These began as **100 candidates**; the Do decisions were
approved for implementation. Decisions are recorded below as **Do, Skip, or Defer**, with their reasons. Do keeps an idea for implementation, Skip rejects
it, and Defer leaves it for later. IDs remain stable after sorting.
All 100 decided: **32 Do, 25 Defer, 43 Skip**. CP-001 to CP-020 were decided
interactively; CP-021 to CP-100 follow the same rules, recorded with their reasons below.

Source review: Commit Plus at
[`b70bf1f`](https://github.com/Commit-Plus/commit-plus/commit/b70bf1f67908926a18113757d0fa92da0bd979cd),
its [documentation index](https://docs.commitplus.app/llms.txt), six feature guides,
and the product-page text supplied in this conversation. Compared with floatinghotel
source at `1a2fcf3` and the existing backlog above. This was targeted source inspection,
not an exhaustive audit or an application/benchmark run.

**Extension** builds on an existing floatinghotel capability. **Experiment** requires
measurement before adoption. **New** is a reader/workspace candidate not identified
in the inspected implementation. **Scope expansion** needs a product decision beyond
the current reader; it is not implicitly authorized by inclusion here. Sources link
to the inspected mechanism or workflow; floatinghotel-specific adaptations are ideas,
not claims that Commit Plus ships those exact behaviors.

Existing tabs, staged/unstaged review, wrapping, Find, bookmarks, comparisons, history
pagination, bounded caches, and basic Git actions are not counted as missing features.
When a candidate overlaps an earlier todo, refine that task after prioritization rather
than creating a second implementation task. Keep review feedback separate from staging,
quiet startup, the native ECS renderer, and current cache budgets.

Documentation has mixed freshness: the introduction labels AI as coming soon, while
the detailed guide and source implement it; the PR guide covers less than the current
source. Guided worktree workflow and several drag targets remain roadmap language.
The site's under-1-second startup, roughly 90 MB idle memory, and roughly 20 MB download
are advertised figures, not measurements from this review. Do not copy error-to-empty
fallbacks as evidence of success, and do not treat “Undo anything” as an unconditional
recovery guarantee. The supplied comparison page also claims every feature is free,
while its pricing and AI guide describe Pro restrictions; do not reuse those claims
or its relative performance assertions as verified competitive evidence. These are
design ideas; no Commit Plus code was copied.

**Implementation status — September 19:** the 32 Do decisions are implemented and
verified. CP-006 is prepared as an upstream-only patch; it has not been adopted in
floatinghotel or submitted upstream. Skip and Defer decisions remain unchanged.
The [implementation report](docs/triage-implementation.md) records behavior,
verification, remaining performance limits, and a screenshot contact sheet.

### Loading and performance

1. **CP-001: Share duplicate Git reads.** Let simultaneous requests for the same revision and options share one in-flight read. Extend existing caches and workers; measure whether duplicate subprocesses contribute to slow commit opening.
   *Experiment. Decision: Do — serves the P0 reading-latency work; small change on existing caches/workers.* [Source][cp-source-001]

2. **CP-002: Show local branches before remote branches finish.** Populate branch pickers incrementally so a slow remote-list lookup does not delay selecting a local review destination.
   *Extension. Decision: Do — cheap responsiveness win; local list is ready before any remote lookup.* [Source][cp-source-002]

3. **CP-003: Load repository badges independently.** Show branch, changed-file count, and ahead/behind as each arrives. Preserve an unknown state instead of showing a misleading zero.
   *Extension. Decision: Do — the unknown-versus-zero correctness point stands on its own.* [Source][cp-source-003]

4. **CP-004: Cache history by filter.** Restore each branch/search filter's history snapshot and selected commit immediately, within a bounded budget. Extend current history loading rather than retaining unlimited snapshots.
   *Experiment. Decision: Defer — real memory cost against an unmeasured benefit; revisit after P0 instrumentation.* [Source][cp-source-004]

5. **CP-005: Fetch just the selected file's patch first.** Compare loading changed-path metadata plus the selected patch with today's whole-commit patch load. Keep whole-review progress and All files available.
   *Experiment. Decision: Defer — plausible win for large commits, but it restructures commit loading; needs the P0 measurements first.* [Source][cp-source-005]

6. **CP-006: Tune reader overscan against blank frames.** Benchmark a small buffer around visible rows during rapid scrolling. Extend existing virtualization; account for wrapping and variable heights instead of copying fixed 20-pixel rows.
   *Experiment. Decision: Do, as upstream work — overscan belongs with the variable-height virtualization item in the
      [upstream-gap ledger](docs/afterhours-gaps.md), not as app-side scroll code.* [Source][cp-source-006]

7. **CP-007: Keep syntax work off invisible preview rows.** Profile syntax work during preview and tab switching; reuse bounded tokens for visible content. Overlaps existing token-cache and offscreen-diff performance work.
   *Experiment. Decision: Do — fold into the existing token-cache and offscreen-diff P0 task rather than tracking twice.* [Source][cp-source-007]

8. **CP-008: Cache graph drawing by visible row.** Precompute reusable graph segments so scrolling does not rebuild unrelated lanes. Extend the existing commit graph and verify merge edges at pagination boundaries.
   *Experiment. Decision: Defer — graph drawing has not shown up as a cost, and merge edges at pagination boundaries make it fiddly.* [Source][cp-source-008]

9. **CP-009: Make cancellation visibly complete.** Give long reads and Git operations a clear Cancelling state that ends when their work stops. Extend existing async cancellation and network-progress tasks.
   *Extension. Decision: Do — honesty fix on cancellation that already exists; a cancelled operation must not look finished while it runs.* [Source][cp-source-009]

10. **CP-010: Benchmark Commit Plus on identical review journeys.** Measure both apps' cold start, idle memory, commit opening, and scrolling on the same fixtures. Treat advertised under-1-second startup and roughly 90 MB idle memory as unverified claims.
   *Experiment. Decision: Defer — build the px0 comparison runner first, then add Commit Plus as a second target.* [Source][cp-source-010]

### Finding and reading history

11. **CP-011: Search commits, paths, branches, and tags together.** Extend revision-aware Quick Open with typed result categories, while retaining repository-content search as a separate operation.
   *Extension. Decision: Do — real reader capability on existing revision-aware Quick Open; content search stays separate.* [Source][cp-source-011]

12. **CP-012: Switch search categories without rerunning everything.** Offer keyboard-accessible category filters over the retained result set, preserving query and focus when switching categories.
   *Extension. Decision: Do — near-free once CP-011's typed categories exist; ship them together.* [Source][cp-source-012]

13. **CP-013: Recognize a pasted commit hash in Quick Open.** Resolve a full or abbreviated object ID directly into a review preview; distinguish ambiguous hashes from no match.
   *Extension. Decision: Do — small addition to CP-011's categories and a common real move.* [Source][cp-source-013]

14. **CP-014: Annotate file search results with change status.** Show modified, added, deleted, or renamed alongside paths, using the selected revision's scope rather than always showing checkout status.
   *Extension. Decision: Do — revision-scoped status is the correct behavior; checkout status against a historical revision would be wrong.* [Source][cp-source-014]

15. **CP-015: Add explicit history branch scopes.** Offer current branch, chosen branch, and all refs, with a separate include-remotes choice. Keep each scope's reading state.
   *Extension. Decision: Do — removes ambiguity about what history is showing; each scope keeps its reading state.* [Source][cp-source-015]

16. **CP-016: Use compact commit-details popovers.** Expose full message, exact timestamp, parents, refs, and hash on demand. Extend the existing compact header without enlarging normal commit rows.
   *Extension. Decision: Do — detail on demand keeps the single-line commit row rule intact.* [Source][cp-source-016]

17. **CP-017: Select multiple commits conventionally.** Support Shift ranges and Cmd toggles in history, maintaining stable hash identities through filtering and pagination.
   *Extension. Decision: Do — prerequisite for CP-018/019; stable hash identity through filtering and pagination is the real work.* [Source][cp-source-017]

18. **CP-018: Review a selected commit range as one change.** Turn a contiguous history selection into an aggregate comparison with feedback. Adapt multi-selection to the existing comparison reader; show exactly which endpoints define the diff.
   *Extension. Decision: Do — "what changed across these commits" is a core review question and comparisons already exist.* [Source][cp-source-018]

19. **CP-019: Copy selected commit hashes or subjects in order.** Offer bulk copy from a multi-commit selection, preserving visible order and keeping single-commit copy available.
   *Extension. Decision: Do — trivial once multi-selection exists, and it feeds the agent-prompt copy workflow.* [Source][cp-source-019]

20. **CP-020: Compare a tag against the current review.** Add a tag-based entry point into existing revision comparisons; resolve the tag to an object ID so later tag movement cannot alter the open review.
   *Extension. Decision: Do — small addition to existing comparisons; resolving to an object ID prevents a real staleness bug.* [Source][cp-source-020]

### Working changes and conflict review

21. **CP-021: Attach one feedback item to disjoint changed lines.** Adapt Cmd-click line selection into a review range set, keeping plain text selection separate. Existing selected-line staging is not new work.
   *Extension. Decision: Do — disjoint-line feedback is core review work, and selected-line ranges already exist.* [Source][cp-source-021]

22. **CP-022: Review a stash like a commit.** Open its files and diffs in a retained review tab with local feedback, without applying it. Identify staged, working, and untracked portions explicitly.
   *Extension. Decision: Do — read-only and it reuses the commit reader; stashes are currently a blind spot.* [Source][cp-source-022]

23. **CP-023: Preview merge conflicts before updating.** Run an on-demand integration analysis against the chosen base and show likely conflict paths without changing the checkout. Report failed analysis as unknown.
   *New. Decision: Defer — useful and read-only, but integration analysis is a sizeable new subsystem; revisit after the P0 work.* [Source][cp-source-023]

24. **CP-024: Distinguish overlapping files from predicted conflicts.** Use separate labels for both branches touching a file and Git predicting a conflict; show the refs and analysis freshness behind the warning.
   *New. Decision: Defer — only meaningful once CP-023 exists.* [Source][cp-source-024]

25. **CP-025: Separate upstream lag from base-branch lag.** Show behind-my-remote and behind-main as distinct comparisons, with a direct review action for each. Extend existing ahead/behind counts.
   *Extension. Decision: Defer — worth having, but it depends on CP-023's integration analysis to be more than a second ahead/behind count.* [Source][cp-source-025]

26. **CP-026: Read a three-way conflict preview.** Display base, local, and incoming text with aligned conflict regions in a read-only destination. Keep the originating unstaged or staged review intact.
   *New. Decision: Defer — a read-only conflict reader fits the product, but it is a whole new alignment surface; not now.* [Source][cp-source-026]

27. **CP-027: Navigate only unresolved conflict blocks.** Provide next/previous conflict with a remaining count, across files where useful. Keep resolved blocks inspectable without repeatedly landing on them.
   *New. Decision: Defer — depends on CP-026.* [Source][cp-source-027]

28. **CP-028: Choose conflict sides explicitly.** If file mutation is approved as product scope, offer current, incoming, or both with a result preview. Saving a resolution and staging it must remain separate actions.
   *Scope expansion. Decision: Skip — writes resolved files; mutation is outside the read-only reader.* [Source][cp-source-028]

29. **CP-029: Open exact revisions in an external diff tool.** Prepare temporary before/after files and launch the chosen installed tool. Extend external-open actions to preserve historical paths and clean up temporary content.
   *New. Decision: Skip — handing diffs to an external tool works against being the diff reader.* [Source][cp-source-029]

30. **CP-030: Hand actual conflicts to an external merge tool.** Supply base, current, incoming, and result paths; detect the result on return. Do not equate tool exit with a resolved or staged file.
   *Scope expansion. Decision: Skip — mutation plus an unreliable "did the tool resolve it" contract.* [Source][cp-source-030]

### Undo and recovery

31. **CP-031: Undo staging and unstaging.** Record successful file, hunk, and selected-line index operations as named undo entries. Text-input undo must retain shortcut ownership.
   *Scope expansion. Decision: Defer — staging already exists so undoing it is defensible, but it needs the whole undo-entry model first.* [Source][cp-source-031]

32. **CP-032: Undo a local commit while preserving its contents.** Offer a guarded return to the prior HEAD and restore the message draft, only when the expected commit is still current.
   *Scope expansion. Decision: Skip — history rewriting is outside the reader's scope.* [Source][cp-source-032]

33. **CP-033: Recover a discarded file from a snapshot.** Back up content before explicit discard/removal and provide restoration. Use Git-resolved metadata paths so linked worktrees work, plus bounded retention.
   *Scope expansion. Decision: Skip — no discard flow to protect; a snapshot store would be scar tissue for a feature we do not have.* [Source][cp-source-033]

34. **CP-034: Undo stash lifecycle actions.** Recover dropped stashes and reverse supported save/apply operations using stable stash object IDs, rather than moving stash indices.
   *Scope expansion. Decision: Skip — stash mutation is outside scope.* [Source][cp-source-034]

35. **CP-035: Undo local branch operations.** Restore a deleted branch, prior name, or previous checkout only after validating expected tips and current working state.
   *Scope expansion. Decision: Skip — branch mutation undo is outside scope.* [Source][cp-source-035]

36. **CP-036: Explain why an undo is unavailable.** Show the operation name and failed precondition, preserve the undo entry after failure, and avoid promising universal undo.
   *Scope expansion. Decision: Skip — only meaningful with the undo system we are not building.* [Source][cp-source-036]

37. **CP-037: Browse the local reflog.** Add a searchable, paginated view of reference movements with actor, action, timestamp, and a route to the associated commit review.
   *New. Decision: Do — read-only, revision-aware, and the best recovery answer available without any mutation feature.* [Source][cp-source-037]

38. **CP-038: Recover a reflog entry into a new branch.** Offer a named recovery branch from the selected object, preserving the current checkout until the user explicitly switches.
   *Scope expansion. Decision: Skip — creates refs; the reflog browser (CP-037) already gives the recovery information.* [Source][cp-source-038]

39. **CP-039: Resume interrupted Git workflows.** Show a persistent operation card with the relevant continue/abort actions after conflicts or restart. Keep operation state separate from ordinary review selection.
   *Scope expansion. Decision: Defer — showing that a rebase or merge is in progress is honest and read-only; the continue/abort actions are not.* [Source][cp-source-039]

40. **CP-040: Preview a guarded force push.** Show destination, expected remote tip, and commits being replaced; use an explicit lease and retain recovery refs. Extend the existing remote-operation backlog.
   *Scope expansion. Decision: Defer — refine the existing force-push confirmation todo instead of adding a second task.* [Source][cp-source-040]

### Worktrees and task workflows

41. **CP-041: List linked worktrees beside repositories.** Expose branch, path, dirty count, and detached/missing state without opening every worktree's full rendering payload.
   *New. Decision: Do — read-only listing, cheap, and directly useful for parallel agent checkouts.* [Source][cp-source-041]

42. **CP-042: Open a worktree without switching this checkout.** Navigate directly to its own repository workspace and preserve both readers' tabs and feedback.
   *New. Decision: Do — navigation only; both readers keep their tabs and feedback.* [Source][cp-source-042]

43. **CP-043: Label worktrees by task or agent.** Store human-readable local labels such as rendering-fix or agent-2 alongside the branch and path.
   *New. Decision: Defer — a new label store for cosmetics; add it if CP-041/042 prove the worktree list gets crowded.* [Source][cp-source-043]

44. **CP-044: Create an isolated worktree from a review.** Use an existing branch or a new branch at the reviewed object ID; preview the destination folder before creation.
   *Scope expansion. Decision: Defer — creates checkouts; revisit only if worktree browsing lands and the creation gap is felt.* [Source][cp-source-044]

45. **CP-045: Lock a worktree with a reason.** Protect a checkout needed by another task from cleanup, and display its lock reason where removal is offered.
   *Scope expansion. Decision: Skip — worktree mutation, outside scope.* [Source][cp-source-045]

46. **CP-046: Move a worktree while preserving its label.** Use Git's worktree move operation, update local workspace identity deliberately, and retain the old location if the operation fails.
   *Scope expansion. Decision: Skip — worktree mutation, outside scope.* [Source][cp-source-046]

47. **CP-047: Repair a moved worktree location.** Relink an unavailable checkout using Git repair, verify its repository identity, and restore its prior review context.
   *Scope expansion. Decision: Skip — worktree mutation, outside scope.* [Source][cp-source-047]

48. **CP-048: Preview worktree cleanup.** Show dirty, locked, missing, and currently open checkouts before remove/prune; preserve task labels for surviving worktrees.
   *Scope expansion. Decision: Skip — worktree mutation, outside scope.* [Source][cp-source-048]

49. **CP-049: Offer branch-start presets.** Configure feature, bugfix, release, and hotfix bases/prefixes per repository, with an optional new-worktree destination. Avoid imposing Git Flow on every repository.
   *Scope expansion. Decision: Skip — imposes a branching methodology; configuration for a workflow we do not own.* [Source][cp-source-049]

50. **CP-050: Preview a task's finish plan.** Display merge targets, strategy, tag, and cleanup steps before executing a recoverable sequence. Generic task workflows are our adaptation, not a claim that Commit Plus's whole roadmap shipped.
   *Scope expansion. Decision: Skip — roadmap language, executes multi-step mutation, no reader value.* [Source][cp-source-050]

### Hosted pull-request review

51. **CP-051: Add a pull-request inbox.** List PRs for the selected remote with number, title, source/target, update time, and open/draft/closed/merged state.
   *Scope expansion. Decision: Defer — hosted PR review is a real product direction but needs an explicit decision, not adoption by inclusion.* [Source][cp-source-051]

52. **CP-052: Review PR changes in the existing reader.** Resolve PR base/head identities into a document tab so local comments, Find, and reading anchors work across hosted and local reviews.
   *Scope expansion. Decision: Defer — the compelling half of hosted review: our reader on PR diffs; gated on the CP-051 decision.* [Source][cp-source-052]

53. **CP-053: Show PR checks and merge readiness separately.** Distinguish pending, failing, unavailable, and no-checks states, and avoid treating successful checks as proof that a PR is mergeable.
   *Scope expansion. Decision: Skip — only exists inside a full PR product.* [Source][cp-source-053]

54. **CP-054: Read the PR discussion next to its code.** Keep description and conversation available in a pane without replacing the retained diff destination.
   *Scope expansion. Decision: Skip — only exists inside a full PR product.* [Source][cp-source-054]

55. **CP-055: Publish a reviewed feedback bundle to a PR.** Adapt Commit Plus's comment action to our feedback basket: preview the exact outgoing text and publish only on an explicit send action.
   *Scope expansion. Decision: Defer — local feedback basket to PR comments is the payoff of CP-051/052; decided with them.* [Source][cp-source-055]

56. **CP-056: Explain unavailable PR patches.** Differentiate binary files, truncated provider patches, permission errors, and loading failures; offer local-object reading or browser fallback when applicable.
   *Scope expansion. Decision: Skip — follows from CP-052 if it is ever built; not separately decidable.* [Source][cp-source-056]

57. **CP-057: Preview the branch diff before creating a PR.** Reuse comparison destinations in a PR draft, with editable title/body and explicit source and target branches.
   *Scope expansion. Decision: Skip — PR creation is outside the reader.* [Source][cp-source-057]

58. **CP-058: Choose PR reviewers and assignees.** Provide searchable participant pickers and show existing assignments beside the review, respecting each provider's supported roles.
   *Scope expansion. Decision: Skip — PR metadata editing is outside the reader.* [Source][cp-source-058]

59. **CP-059: Select the provider account per repository.** Bind hosted review and remote actions to the chosen account/remote, with clear expired-account recovery. Reuse the broader hosted-integration backlog.
   *Scope expansion. Decision: Skip — account plumbing for a product we have not approved.* [Source][cp-source-059]

60. **CP-060: Cache PR lists, details, and changes separately.** Bound caches by repository, provider, account, and PR revision; refresh one surface without discarding the others or leaking state across accounts.
   *Scope expansion. Decision: Skip — cache design for a product we have not approved.* [Source][cp-source-060]

### Optional AI assistance

61. **CP-061: Ask AI to explain the current commit.** Add an explicit explanation action alongside the reader, scoped to the pinned review and retaining the selected line.
   *Scope expansion. Decision: Skip — the reader is for humans reading code; no AI surface planned.* [Source][cp-source-061]

62. **CP-062: Ask AI to review unstaged changes.** Offer a deliberate review request against an identified content snapshot. Treat findings as suggestions for the feedback basket, never staging instructions.
   *Scope expansion. Decision: Skip — no AI surface planned.* [Source][cp-source-062]

63. **CP-063: Attach exact file and line citations to AI answers.** Use revision-aware evidence records that reopen the cited source range; clearly label truncated context and unavailable historical objects.
   *Scope expansion. Decision: Skip — no AI answers to cite.* [Source][cp-source-063]

64. **CP-064: Ask AI about a branch comparison.** Supply bounded merge-base diff, commit subjects, paths, and stats for the comparison being reviewed, with links back to evidence.
   *Scope expansion. Decision: Skip — no AI surface planned.* [Source][cp-source-064]

65. **CP-065: Search repository chat history locally.** Store and search conversations per repository, with explicit delete and reopen actions; restore the referenced review where available.
   *Scope expansion. Decision: Skip — no AI surface planned.* [Source][cp-source-065]

66. **CP-066: Choose an on-device or BYOK AI provider.** Keep provider/model selection explicit and keys in platform credential storage. Preserve ordinary local review without an AI account.
   *Scope expansion. Decision: Skip — provider plumbing for a feature we are not building.* [Source][cp-source-066]

67. **CP-067: Draft an editable commit message.** Generate from the explicitly selected staged or unstaged snapshot plus recent subjects, with bounded context. Extend the existing commit-template idea.
   *Scope expansion. Decision: Defer — the only AI item with a local hook: refine the existing commit-template todo if the commit editor stays.* [Source][cp-source-067]

68. **CP-068: Reject AI answers whose source changed.** Compare content fingerprints, active scope, and provider when a result arrives; retain drafts and offer regeneration rather than silently using stale analysis.
   *Scope expansion. Decision: Skip — staleness guard for AI results we will not have.* [Source][cp-source-068]

69. **CP-069: Bound repository AI exploration.** Use app-owned read tools with command/time/output limits and cancellation. Independently validate supported Git arguments; do not copy a broad command-name allowlist as a complete read-only guarantee.
   *Scope expansion. Decision: Skip — an agent harness is a separate product, not a reader feature.* [Source][cp-source-069]

70. **CP-070: Ask for human choices when conflict intent is ambiguous.** Present AI-proposed choices with evidence and preview the resulting patch. Our adaptation must keep applying and staging explicit, unlike automatic resolution-and-stage flows.
   *Scope expansion. Decision: Skip — AI conflict resolution is two scope expansions stacked.* [Source][cp-source-070]

### Direct Git interactions

71. **CP-071: Drag commits onto a branch to propose cherry-pick.** Show the selected commits and destination before execution, with an equivalent menu command and an unambiguous cancellation path.
   *Scope expansion. Decision: Skip — drag-to-mutate history is a footgun in a review tool.* [Source][cp-source-071]

72. **CP-072: Drag a branch to propose merge or rebase.** Use a labeled drop preview that updates with the modifier key, so the intended action is visible before release.
   *Scope expansion. Decision: Skip — drag-to-merge/rebase is a footgun in a review tool.* [Source][cp-source-072]

73. **CP-073: Drag a commit to create a branch or tag.** Seed the creation form with the dragged object ID and keep creation separate from checkout.
   *Scope expansion. Decision: Skip — menus already create branches and tags without a drag target.* [Source][cp-source-073]

74. **CP-074: Drag files to stage or stash.** Provide deliberate staging/stash drop targets with a preview and keyboard equivalent. Documentation marks broader file drops as roadmap; do not assume all targets are shipped.
   *Scope expansion. Decision: Skip — keyboard and menu staging already exist; drag adds risk, not reach.* [Source][cp-source-074]

75. **CP-075: Squash a valid contiguous commit selection.** Preview the rewritten series and editable message; reject unsupported merge/noncontiguous selections and preserve a recovery point.
   *Scope expansion. Decision: Skip — history rewriting is outside scope.* [Source][cp-source-075]

76. **CP-076: Make push destination explicit.** Prefer the branch's configured upstream remote, show remote and destination branch together, and handle differing fetch/push URLs. Extend existing push controls.
   *Extension. Decision: Do — correctness on an existing control: show remote and destination branch, handle differing fetch/push URLs.* [Source][cp-source-076]

77. **CP-077: Manage tracking from the branch context menu.** Expose set/change/unset upstream and show the relationship before an action, extending existing branches and ahead/behind presentation.
   *Extension. Decision: Defer — showing the tracking relationship is good; set/change/unset upstream needs its own decision.* [Source][cp-source-077]

78. **CP-078: Show annotated tag details and remote actions.** Inspect tag message and target, then offer explicit publish/delete destinations per remote rather than an ambiguous global action.
   *Scope expansion. Decision: Defer — tag inspection is read-only and cheap; per-remote publish/delete is not.* [Source][cp-source-078]

79. **CP-079: Expand submodule inspection into useful states.** Distinguish absent checkout, dirty content, and differing gitlink/checkout revisions, with a path into the nested repository. Refines the existing submodule-view todo.
   *Extension. Decision: Do — refines the existing submodule-view todo with the states that actually occur.* [Source][cp-source-079]

80. **CP-080: Track vendored subtrees explicitly.** Maintain prefix, source repository, branch, and squash mode, then review proposed subtree updates before running add/pull/push.
   *Scope expansion. Decision: Skip — subtree management is a separate product.* [Source][cp-source-080]

### Workspace and repository organization

81. **CP-081: Add an on-demand repository attention inbox.** Aggregate conflicts, interrupted operations, divergence, and unavailable folders across recent repositories. Keep clean-state messages out of the normal sidebar.
   *New. Decision: Defer — plausible, but a new cross-repository surface; revisit after the picker work (CP-083..085).* [Source][cp-source-081]

82. **CP-082: Add a local activity overview.** Show recent repository activity and active days when requested, with cached local metadata and no automatic network fetch.
   *New. Decision: Skip — activity dashboards are decoration, not reading.* [Source][cp-source-082]

83. **CP-083: Pin repositories independently of recency.** Keep important repositories in the picker after they fall out of the recent list; retain local availability and remote identity separately.
   *Extension. Decision: Do — small, and recency alone loses repositories you actually care about.* [Source][cp-source-083]

84. **CP-084: Filter and sort the repository picker.** Search name/path, filter by hosting provider, and sort by name or last opened without opening each repository.
   *Extension. Decision: Do — small, and it avoids opening repositories just to find one.* [Source][cp-source-084]

85. **CP-085: Relink a moved repository.** Offer Change folder for unavailable recent entries, verify identity, and preserve tabs and reviews instead of silently treating it as a new repository.
   *Extension. Decision: Do — prevents silently losing tabs and saved reviews when a folder moves.* [Source][cp-source-085]

86. **CP-086: Configure preferred external applications.** Discover installed editors, terminals, diff tools, and merge tools; retain role-specific preferences and show unavailable choices clearly.
   *Extension. Decision: Defer — we skipped external diff/merge tools; revisit if editor and terminal handoff needs real configuration.* [Source][cp-source-086]

87. **CP-087: Customize visible header shortcuts.** Let users hide seldom-used actions while keeping them in accessible menus, preserving compact headers and font sizing independent of UI scale.
   *Extension. Decision: Skip — configuration for a value that does not change; hide-the-button settings are pure surface.* [Source][cp-source-087]

88. **CP-088: Remember sidebar sections per repository.** Extend persisted tree state to any new branches, tags, stashes, or worktree sections, without automatically adding all sections to every workspace.
   *Extension. Decision: Do — cheap extension of persisted tree state for whatever sections exist.* [Source][cp-source-088]

89. **CP-089: Add per-repository refresh preferences.** Allow explicit overrides for refresh-on-activation and optional auto-fetch. Pause or coalesce background work so reading latency stays predictable.
   *Extension. Decision: Defer — the refresh knobs are YAGNI, but coalescing background work for predictable reading latency belongs with the P0 work.* [Source][cp-source-089]

90. **CP-090: Keep author identity local to the repository.** Show inherited versus repository-specific name/email in a settings draft, apply only chosen changes, and avoid altering global Git identity accidentally.
   *Scope expansion. Decision: Skip — editing Git identity is outside the reader.* [Source][cp-source-090]

### Architecture, diagnostics, and delivery

91. **CP-091: Separate operation planning from execution.** Represent a concrete Git action as a validated plan reused by menus, shortcuts, drag/drop, and future AI, while keeping repository-owned ECS state.
   *Extension. Decision: Skip — an abstraction whose consumers (AI, drag/drop) we just rejected.* [Source][cp-source-091]

92. **CP-092: Scope commands to the invoking workspace.** Extend existing typed navigation and focus routing to every future menu/window action so another repository cannot receive it after focus changes.
   *Extension. Decision: Do — a real correctness bug class: a command landing in the wrong repository after focus changes.* [Source][cp-source-092]

93. **CP-093: Isolate provider services behind common review models.** Keep GitHub/GitLab transport outside the renderer and normalize PR capabilities. Test provider differences without duplicating the reader.
   *Scope expansion. Decision: Skip — provider abstraction for a product we have not approved.* [Source][cp-source-093]

94. **CP-094: Create an opt-in diagnostics bundle.** Export app/Git versions, runtime paths, command durations, and relevant cache statistics with deliberate redaction. Extend existing logs and probes; do not include repository content by default.
   *Extension. Decision: Defer — useful once the P0 measurement work produces numbers worth exporting.* [Source][cp-source-094]

95. **CP-095: Redact credentials before logs are stored.** Audit existing and future command logging for authenticated URLs, headers, and token arguments, with fixture-based regression checks.
   *Extension. Decision: Do — security: command logging exists today and can capture authenticated URLs and tokens.* [Source][cp-source-095]

96. **CP-096: Show the active Git executable and capabilities.** Report Git path/version and unsupported operations clearly. Consider an optional managed runtime separately from the default system Git path.
   *Extension. Decision: Do — small, and Git version differences already cause behavior we have to debug.* [Source][cp-source-096]

97. **CP-097: Sync only portable preferences, optionally.** Separate themes and durable remote bookmarks from local paths, worktree state, and credentials; offer an explicit first-sync choice. Extends the broader synchronization discussion.
   *Scope expansion. Decision: Skip — settings sync is a separate product with credential and path hazards.* [Source][cp-source-097]

98. **CP-098: Make new local stores versioned and failure-aware.** Use atomic writes and distinguish missing, corrupt, and newer-schema data. Apply this to review/workflow stores without replacing existing schemas indiscriminately.
   *Extension. Decision: Do — overlaps the existing atomic-write and review-key work; corrupt-versus-missing must be distinguishable.* [Source][cp-source-098]

99. **CP-099: Make operation tests deterministic and offline.** Inject Git/provider/clock boundaries for new workflows and verify failures, stale state, and out-of-order completion using disposable fixtures. Extend the existing behavioral suite.
   *Extension. Decision: Do — extends the existing behavioral suite; deterministic offline fixtures are how the rest is verified.* [Source][cp-source-099]

100. **CP-100: Ship verifiable native updates.** Prepare signed/notarized releases, update metadata, and install/rollback verification, with release notes on demand. Refines existing packaging work while preserving quiet startup.
   *Scope expansion. Decision: Defer — refines the existing packaging todo; sequence it with the release-platform decision.* [Source][cp-source-100]

### Commit Plus source references

[cp-source-001]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/BranchListCache.swift#L49
[cp-source-002]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/History/BranchFilterBar.swift#L252
[cp-source-003]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/MainWindow/RepoPickerView.swift#L712
[cp-source-004]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/History/HistoryView.swift#L1036
[cp-source-005]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/History/HistoryView.swift#L1316
[cp-source-006]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Models/CommitFilePreviewViewport.swift#L20
[cp-source-007]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/History/CommitFilePreviewHighlightCache.swift#L20
[cp-source-008]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/History/CommitGraphRowSlice.swift#L23
[cp-source-009]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/App/RepositoryOperationProgress.swift#L123
[cp-source-010]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitCommandLogStore.swift#L46
[cp-source-011]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Models/SearchResult.swift#L20
[cp-source-012]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/ViewModels/SearchCoordinator.swift#L81
[cp-source-013]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitStatusService+Search.swift#L50
[cp-source-014]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitStatusService+Search.swift#L129
[cp-source-015]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/History/BranchFilterBar.swift#L25
[cp-source-016]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/History/CommitInfoPopoverView.swift#L25
[cp-source-017]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/History/HistoryCommitSelection.swift#L20
[cp-source-018]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/History/HistoryCommitSelection.swift#L42
[cp-source-019]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/History/HistoryView.swift#L1008
[cp-source-020]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/MainWindow/Sidebar/SidebarTagContextMenu.swift#L43
[cp-source-021]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/Common/DiffView.swift#L492
[cp-source-022]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/Stashes/StashView.swift#L167
[cp-source-023]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitStatusService+CurrentBranchIntegration.swift#L213
[cp-source-024]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Models/PotentialConflictFileAnalysis.swift#L21
[cp-source-025]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitStatusService+CurrentBranchIntegration.swift#L64
[cp-source-026]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/ConflictPanelAlignment.swift#L20
[cp-source-027]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/ConflictNavigationState.swift#L20
[cp-source-028]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/Common/ConflictMergeToolView.swift#L25
[cp-source-029]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/ExternalDiffFileService.swift#L21
[cp-source-030]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/ExternalMergeFileService.swift#L21
[cp-source-031]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitUndoModels.swift#L136
[cp-source-032]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitUndoModels.swift#L172
[cp-source-033]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitFileUndoSnapshotStore.swift#L25
[cp-source-034]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitUndoModels.swift#L58
[cp-source-035]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitUndoExecutor.swift#L150
[cp-source-036]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitUndoExecutor.swift#L25
[cp-source-037]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitStatusService+Reflog.swift#L22
[cp-source-038]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/Reflog/ReflogDetailPanel.swift#L81
[cp-source-039]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitFlowRecoveryStore.swift#L21
[cp-source-040]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitStatusService+BranchForcePush.swift#L37
[cp-source-041]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitStatusService+Worktree.swift#L34
[cp-source-042]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/MainWindow/Sidebar/SidebarWorktreeContextMenu.swift#L36
[cp-source-043]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/WorktreeLabelStore.swift#L20
[cp-source-044]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitStatusService+Worktree.swift#L110
[cp-source-045]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitStatusService+Worktree.swift#L182
[cp-source-046]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitStatusService+Worktree.swift#L213
[cp-source-047]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitStatusService+Worktree.swift#L169
[cp-source-048]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitStatusService+Worktree.swift#L143
[cp-source-049]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitFlowPlanner.swift#L38
[cp-source-050]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitFlowPlanner.swift#L77
[cp-source-051]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/PullRequests/PullRequestListView.swift#L375
[cp-source-052]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/PullRequests/PullRequestChangesView.swift#L21
[cp-source-053]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Models/PullRequestModels.swift#L80
[cp-source-054]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/PullRequests/PullRequestListView.swift#L530
[cp-source-055]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/PullRequests/PullRequestListView.swift#L771
[cp-source-056]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/PullRequests/PullRequestChangesView.swift#L159
[cp-source-057]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/PullRequests/CreatePullRequestSheet.swift#L164
[cp-source-058]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/PullRequests/PullRequestMetadataSidebar.swift#L21
[cp-source-059]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/App/PullRequestController.swift#L276
[cp-source-060]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/App/PullRequestController.swift#L22
[cp-source-061]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/RepositoryAIPrompt.swift#L21
[cp-source-062]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/RepositoryAIFileContextService.swift#L42
[cp-source-063]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/RepositoryAIFileContextService.swift#L76
[cp-source-064]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/RepositoryAIRepositoryAnalysisService.swift#L28
[cp-source-065]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/RepositoryAIChatHistoryStore.swift#L24
[cp-source-066]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/AIProviderRegistry.swift#L20
[cp-source-067]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/CommitMessageContextBuilder.swift#L20
[cp-source-068]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/RepositoryAIFileContextService.swift#L155
[cp-source-069]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/RepositoryAIAgentHarness.swift#L39
[cp-source-070]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/ConflictAIResolutionPlanApplier.swift#L26
[cp-source-071]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitDragDropPolicy.swift#L46
[cp-source-072]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitDragDropPolicy.swift#L93
[cp-source-073]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitDragDropPolicy.swift#L64
[cp-source-074]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitDragDropPolicy.swift#L145
[cp-source-075]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/History/HistoryView.swift#L1265
[cp-source-076]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitStatusService+BranchForcePush.swift#L22
[cp-source-077]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/MainWindow/Sidebar/SidebarBranchContextMenu.swift#L21
[cp-source-078]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/MainWindow/Sidebar/SidebarTagContextMenu.swift#L21
[cp-source-079]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitStatusService+Submodule.swift#L268
[cp-source-080]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitStatusService+Subtree.swift#L161
[cp-source-081]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Models/WelcomeRepositoryAttention.swift#L20
[cp-source-082]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/ViewModels/WelcomeDashboardModel.swift#L58
[cp-source-083]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/MainWindow/RepoPickerView.swift#L102
[cp-source-084]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/MainWindow/RepoPickerView.swift#L237
[cp-source-085]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Views/MainWindow/RepoPickerView.swift#L191
[cp-source-086]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/IntegrationApplicationCatalog.swift#L22
[cp-source-087]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Models/AppSettingsSnapshot.swift#L21
[cp-source-088]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/SidebarSettingsStore.swift#L78
[cp-source-089]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Models/RepoSettings.swift#L30
[cp-source-090]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Models/RepoSettings.swift#L38
[cp-source-091]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitFlowPlanner.swift#L38
[cp-source-092]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/App/WindowScopedNotification.swift#L21
[cp-source-093]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/PullRequestProviding.swift#L21
[cp-source-094]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/AdvancedDiagnosticsService.swift#L30
[cp-source-095]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitCommandLogStore.swift#L86
[cp-source-096]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitRuntimeManager.swift#L99
[cp-source-097]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/SettingsSyncService.swift#L158
[cp-source-098]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgit/Services/GitFlowRecoveryStore.swift#L35
[cp-source-099]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/macgitTests/BranchListCacheTests.swift#L1
[cp-source-100]: https://github.com/Commit-Plus/commit-plus/blob/b70bf1f67908926a18113757d0fa92da0bd979cd/scripts/release/notarize-and-staple.sh#L1

## Cleanup record

The February implementation checklist, seven refactor plans, early tab/watcher/
copy/layout designs, toolbar audit, unanswered product questionnaire, and duplicate
Afterhours notes were retired after reconciliation. Their history remains in Git.
The original atomic-save proposal has landed upstream; remaining adoption and
power-loss durability work is listed above. Completed selection, copy, folding,
Markdown preview, tabs, split diffs, sticky file headers, source paging, tree
compaction, footer, and window-state tasks were removed from this backlog.

Verification reports, screenshots, recorded failures, the current navigation
contract, and the detailed upstream-gap ledger remain under `docs/`.
