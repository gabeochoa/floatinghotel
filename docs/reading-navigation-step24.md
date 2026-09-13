# Step 24: preserve tree position through refresh and filtering

Tree list updates retain a path and fractional row offset instead of relying
on a row index. Existing focused rows retain their path; disappearing rows
use the nearest surviving row from the previous order. Compact directory
replacements retain the corresponding directory identity.

A short filtered list may clamp scrolling to its top. The intended path is
retained until the user scrolls, so clearing that filter can restore the
same logical position. Empty results retain the anchor for returning rows.
Refreshing an offscreen focused row does not request a viewport reveal;
explicit keyboard navigation still does.

All 20 tree unit tests pass. The native refresh journey passed at 100%, 140%,
and 200% zoom. Six before/after comparisons preserved focused-row coordinates
within 0.001 px after insertions. Filtering, empty results, and manual scrolling
followed by refresh also passed. Refresh did not append navigation visits.
The p99 frame samples were 10.37, 10.94, and 6.44 ms respectively.

The first keyboard regression attempt exceeded the 20 ms gate at 140%
(32.26 ms p99, 100.47 ms maximum). That run remains in
`output/step24-tree_keyboard`. The reveal regression then caught focus moving
from the Review button into a file row when switching sidebar views. Focus
restoration now requires a target from the previous row sequence. The failed
replay is `output/step24-final-tree_reveal`; the corrected replay passes at all three zooms.

The compact-tree, focus-return, reveal, and keyboard regression journeys all
pass at the three zoom levels. Navigation ownership checks also pass.

Evidence: `docs/reading-navigation-evidence/step24` (layout/workspace JSON is
gzip-compressed). Full replay: `output/step24-verified-tree_refresh_position`.
Binary SHA-256: `de85ba57552f469e84636609a814a8a082b0b8a66fc43b4f3044380bbc0f5447`.
