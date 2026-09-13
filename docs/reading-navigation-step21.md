# Step 21: preserve folds when selecting files

Selecting a changed-file row no longer erases file or hunk folds. The reader
still reveals the file header. Source excursions and Back/Forward retain the
same review's fold state. Find expands the file and matching hunk only when
navigating to a match; retaining a query does not continuously undo folds.

The original behavior was reproduced in `output/step21-before`: adding a
comment folded a hunk, then selecting its file expanded it. The native replay
also covers a second commented file, file-level folding, source return,
history, and Find. The completed journey passes at 100%, 140%, and 200%
zoom, with 120-frame p99 samples of 3.32, 2.79, and 2.32 ms. The Git index
and working diff are unchanged throughout.

The replay collapses the first file before commenting on the second, so
its generic Comment selector cannot hit the first file again. At 200%,
after returning to a hunk, it scrolls to the file header before testing its
fold control. The earlier test attempts are retained in `output/step21-before`
and `output/step21-folds`.

All 13 logical-anchor layout comparisons pass, with errors below 0.001 px.
Source-origin journeys pass at all three zooms. Comment-jump and navigation
boundary regressions pass, as do all 20 review-storage unit tests.

Evidence: `docs/reading-navigation-evidence/step21`.
Layout and workspace JSON captures are gzip-compressed without modification.
Full replay: `output/step21-folds-final`.
Binary SHA-256: `e82ed6a7d874fb9461a6f91cd7ea92597f191330185339f823c8d94dbbc961d5`.
