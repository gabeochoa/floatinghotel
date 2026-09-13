# Step 12: source at the diff location

Open file uses the selected code position when it belongs to that file.
Otherwise it chooses the first visible changed line, then falls back to the
first change in the file. Deleted lines use the before revision and a renamed
file's old path. Added lines use the after revision. Whole-file deletions also
open the before revision.

Source destinations retain a decoded character column. Their originating
review location and viewport anchor are separate from the source identity and
reading anchor. Return to review restores that exact origin even after another
source or a later visit changed the review tab's position. Session storage
preserves the origin anchor and column.

The renderer now stamps anchor reads with repository, document, generation,
and data version. A navigation action inside the render pass cannot publish
the old review's anchor into the newly opened source. Screen and clipped
rectangle helpers are shared by interaction code and layout diagnostics.

## Verification

51 navigation tests and five session tests passed.
`tests/source_origin.py --output NEW_DIRECTORY` passed at 100%, 140%, and 200%.
It verifies a selected deleted line and column in a renamed file, a completely
deleted file opened without a selection, resolved before objects, unchanged
working files, and exact review-return geometry within two pixels.

History replay, all seven navigation regressions, and session restart journeys
also passed at all three applicable zooms. Source-origin replay p99 frame times
were 4.85, 1.87, and 4.73 ms. Binary SHA-256:
`beb56ded26e147b490fd3da27d090c626877e57688d9b9b6664c719586d92bd9`.
Compact screenshots, geometry, and navigation snapshots are in
`docs/reading-navigation-evidence/step12`; full captures and logs are in
`output/step12-source_origin` and `output/step12-native.log`.

The initial compile found the clipped-rectangle helper was defined only after
the diff renderer through the diagnostic header. It now lives in a shared
geometry header used by both interaction code and layout diagnostics.
