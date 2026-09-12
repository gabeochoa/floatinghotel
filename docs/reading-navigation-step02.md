# Step 02: typed navigation

Status: complete; verified before commit.

`ReadingWorkspace` owns the retained review, source, and event history. Tree rows, content tabs, Quick Open, search results, bookmarks, comment links, commit history/search, merge-parent choices, review queues, and native Back/Forward call the same navigation boundary. Renderer reads use const queries; the old selection fields and frame-sampled history are removed.

Source destinations distinguish working tree, index, pending historical references, and resolved object IDs. A worker resolves historical references before reading their files. A matching completion pins the source and its history visit to the resulting commit. Commit metadata uses that same resolved commit as the patch. Comparison form inputs remain separate from loaded comparison identity. Source keeps its originating review separately, and opening source preserves the retained review's storage scope.

Each reader records repository, destination, request key, navigation generation, and repository data generation. Result application rejects a superseded stamp. The workspace is private to `RepoComponent`; only the navigation boundary can mutate it. Re-selecting the same destination preserves work, Find, focus, and selection. An invoking picker or search panel can still dismiss.

The two-slot presentation, existing cache budgets, and pixel restoration remain for their later numbered commits.

## Checks

- 25 diff/navigation unit checks passed.
- 20 content-reader checks passed, including resolving HEAD, deleting the file in a new commit, and reading its original contents after Back.
- 19 review-store checks passed.
- 14 target checks passed, covering stale requests, canonical history, retained comparison storage, no-op selection, and consumed source destinations.
- 4 commit-patch checks passed.
- `python3 tests/check_navigation_boundary.py` and `git diff --check` passed.

Logs are in `output/navigation-design`. Source was frozen before the final consistent native build. An accidentally started replay overlapped that build; it is excluded from verification. The regression harness now records the binary hash before execution and rejects a binary change during its run.

## Review findings fixed

The independent GPT-5.6-sol review found four issues in the first implementation: equal destinations still reset UI state, metadata could resolve an alias differently from its patch, source line had a second mutable copy, and workspace mutations were publicly callable. Those were fixed and rechecked. The first saved-comparison unit run also caught missing restoration of the comparison input fields. A source ownership check found that source navigation from a comparison must retain the comparison's review storage. These failures are retained in the local logs.

The first native replay, `output/reading-navigation/step02-probe`, failed. A review file path had incorrectly triggered the working-tree renderer despite the retained commit identity. MainContentSystem now branches on the typed review destination. That failure also exposed the sidebar-heading probe issue documented in the baseline report.

The legacy Quick Open and search scripts exposed a second presence check in LayoutUpdateSystem. With no retained review file, a source document was incorrectly collapsed into dock mode. The shelf now checks the active source destination. Menu-based legacy scripts must run with `FH_NATIVE_MENUS` absent; its presence enables native menus even when its value is `0`.

The cross-family audit found that changing comparison form inputs during a pending read did not invalidate that read. Submitted form requests now validate current Base, Target, merge mode, and diff options. Automatic reloads validate the typed comparison and options independently of unsubmitted edits. Both success and error publication use the same guard; the added tests and review passed.

## Final native evidence

The final binary (`a1d787d3a3bc1d1fd5d9f4877db151e24bddd8823f0bc8fde078f61e8170f2cb`) passed all seven navigation regression scripts, including 11 reader-heading, tab-geometry, and PNG checks. The same binary passed nine repeated reading journeys at 100%, 140%, and 200% zoom: 90 cold/warm samples with destination and geometry assertions. See `output/navigation-design/regressions-final-binary` and `output/reading-navigation/step02-final-binary`.

Maximum synthetic selection feedback was 13.53 ms. Maximum destination readiness was 1553.98 ms. Eighteen of 45 warm samples exceeded 100 ms, so the warm switching target remains unmet. All nine render p99 measurements passed the existing 20 ms gate (3.56–6.05 ms). Peak owned content was 16,993 bytes; blob and patch cache contents peaked at 4,101 and 29,719 bytes respectively, within unchanged budgets. The preceding 90-sample run reached 2123.24 ms readiness; variability remains in `output/reading-navigation/step02-verified`.

These timings measure synthetic input dispatch through CPU rendering, not physical input or GPU presentation. The corrected reader-heading predicate differs from step 01's commit predicate, so commit readiness is not a direct before/after performance comparison. This commit establishes navigation ownership and identity; it does not claim the plan's final latency targets.

Durable summaries, all 82 targeted unit results, native regression results, and selected screenshots/layouts are in `docs/reading-navigation-evidence/step02`. Complete local runs retain every screenshot and layout. The deferred tab collection and later reading behaviors remain unchecked in the plan.
