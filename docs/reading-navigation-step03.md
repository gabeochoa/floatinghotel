# Step 03: independent document tabs

Status: complete; verified before commit.

The workspace stores a collection of documents with stable IDs and one active ID. Review identity excludes the selected file; source identity excludes the requested line and originating review. Selecting a different file inside a review updates its reading location. Opening another review or source preserves existing documents. Tab activation uses document IDs, and Back can reopen a closed source.

Tabs contain destinations, reading locations, activation recency, and optional file-list summaries. Worker tasks and rendering bodies remain outside the collection. Leaving a source releases its bytes, decoded text, page diff, Markdown/hex payloads, and blame content. Leaving a commit releases its patch and message runtime. Source documents retain lightweight origin-review file metadata so the sidebar remains useful. Existing bounded blob and patch caches retain their budgets.

Step 04 adds preview/keep behavior; step 05 improves titles; step 06 handles overflow; step 07 generalizes close/reopen controls. The existing source close action remains available during this step.

## Verification

The final native build and mouse journeys passed. The targeted state suite passes 21 checks, including six independently selectable documents (working changes, two commits, three files), same-document location updates, and payload release with retained file summaries. The 25 existing diff/navigation, 19 review-store, and 20 content-reader checks also pass (85 total). The native runner `tests/document_tabs.py` opens and reselects all five requested destinations at 100%, 140%, and 200%, checks stable IDs and actual reader headings, and asserts tab geometry and inactive payload absence.

## Findings during verification

A sidebar row must defer navigation until the virtual list finishes rendering: opening a review can clear the origin-summary vector that the current row references. Selection now queues a path and applies it after rendering the list.

The independent review found that releasing a comparison when activating another review left its loaded identity behind. The new unit reproduced the retained vector capacity and missing reload. Verification will rerun after the marker and payload are released together.

Historical aliases now coalesce into an already-open resolved document, retaining its ID and the requested reading location. Without coalescing, resolving a branch to an open commit could leave a duplicate tab that activated the first matching tab instead.

File summaries retain change kind, old path, review signatures, and hunk keys. This keeps Added/Deleted/Renamed filters, comments on a renamed old path, and review progress correct while source is active, without retaining hunk text. The first compact summary omitted these fields; review caught the filter regression before commit.

Direct commit switches now reset the aggregate runtime before starting the new read, rather than retaining the previous message and vector capacities. Resolving a review also updates matching origins in retained source tabs and source history visits, preserving their file positions. This avoids a source returning to an obsolete unresolved alias.

Independent review completed after the listed fixes with no remaining step-03 blockers.

The first native runner used nonexistent fixture path `src/utils.cpp`, so Quick Open correctly left the third source unopened and the six-document assertion failed. The fixture has `src/app.cpp`; the runner was corrected without a product change. Failed evidence is retained in `output/document-tabs/native`.

## Final native results

Binary `4ffa03eb52d0288003027c3908b3a0666880082a183a952a944328d60ad005d4` passed the three-zoom document journey: six tabs, six destination checkpoints per zoom, stable identities, nonoverlapping visible tab rectangles, correct reader headings, and no inactive rendering payloads. All seven navigation regression scripts passed, including 12 geometry checkpoints and reopening a comparison after visiting another review. Native render p99 was 4.93 ms at 100%, 4.23 ms at 140%, and 19.79 ms at 200%; all passed the 20 ms gate, with the 200% result close to the limit.

A separate cold/warm reading journey at 100% passed all ten checkpoints. Maximum selection feedback was 6.88 ms, maximum readiness 749.91 ms, and peak owned content 15,245 bytes versus 16,993 in step 02. Three warm samples exceeded 100 ms; the final switching target remains unmet. Its render p99 was 3.67 ms. Existing cache budgets remain unchanged.

Complete evidence is in `output/document-tabs/native-fixture-corrected`, `output/document-tabs/regressions`, and `output/document-tabs/reading`. Durable results, screenshots, workspace states, selected geometry nodes, timing samples, and unit logs are in `docs/reading-navigation-evidence/step03`. At 200% the titles are cramped; title disambiguation and overflow remain steps 05–06.

The user subsequently prioritized a commit-style view of all unstaged changes, with staged changes separate, and removal of the recurring startup toast. Those requests will be implemented next, before resuming step 04. They do not mark any later numbered item complete.
