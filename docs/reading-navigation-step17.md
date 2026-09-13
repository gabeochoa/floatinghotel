# Step 17: distinct tree states

Tree rows retain their full-row hover and selected backgrounds and the native
focus outline. Review status occupies a fixed 20-pixel column: an unreviewed
ring, a reviewed check, an unresolved-comment count, or a changed-since-viewing
dot. Tooltips give the status text and full unresolved count. Filenames no
longer change color or gain a comment-count suffix in the working-files tree.

The status column uses the existing revision-scoped review records, including
rename-aware comment counts. Historical source tabs calculate reviewed state
from retained file summaries. Summary and loaded-diff review progress share
the same predicate, so opening source does not change the tree's status.

`tests/tree_states.py` passed at 100%, 140%, and 200%. It checks idle, hover,
keyboard focus, selected, selected-plus-focus-plus-hover, and reviewed states;
another row can remain hovered while the selected row has keyboard focus.
It marks and unmarks a file, adds feedback, changes a separate fixture file,
and opens a reviewed historical file as source. Child geometry, filenames,
text colors, and counts stay unchanged across status transitions. The index
stays untouched; the one fixture edit is checked byte-for-byte.

All 53 navigation/review-target unit tests and 20 review-store tests passed.
The seven navigation scenarios, focus return, and source-origin journeys also
passed. Tree replay frame p99 values are in `frame-metrics.json`; each remained
below the 20 ms gate.

The first test attempt looked for the working-review toggle in the Review
sidebar. That control belongs to Files. The corrected journey opens Files
before using it. The Tab probe also showed collection creation order placing
six other controls between the filter and the first file row. The matrix uses
the observed sequence and asserts the resulting semantic focus target. Direct
tree arrow navigation follows in step 18; the framework traversal limitation
is recorded in `docs/afterhours-gaps.md`.

Evidence: `docs/reading-navigation-evidence/step17`.
Full replay: `output/step17-tree-second`.
Binary SHA-256: `7fd908616922b65cc7929b012ec76511d0c0762041eab230b0699b929c21ae3b`.
