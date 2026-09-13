# Step 46: read-only keyboard selection

Code focus owns arrow movement, Shift extension, word movement, line boundaries,
and the active file's document boundaries. Left/right movement uses composed
character boundaries; vertical movement follows rendered rows and preserves the
preferred horizontal position. A plain left/right arrow collapses a selection.

Movement updates the caret and reading anchor without adding a history visit.
Offscreen destinations use layout-ready anchor restoration and scroll only when
needed. Page-edge and document-end commands reuse the bounded source reader,
with its repository/document/request generation checks. A canceled document
releases pending caret work. Bookmarks and Open source follow the new position.

Option+Arrow moves by words when Code owns focus. Document-tab focus retains the
history shortcut; text fields keep their own editing behavior. Selection across
unrendered or unloaded content remains step 48's work.

The 43 diff/navigation unit checks pass. Thirty native keyboard journeys cover
source, both sides of unified/split diffs, 100%/140%/200% zoom, and a variant where
insertions shift the before/after line numbers. A before-side endpoint copies
`a.cpp:L43` even though the after-side line is 44. Tests check graphemes, exact
selected bytes, caret visibility, focus, stable history, bookmark destinations,
page transitions at line 4,096, line 6,000, and the end of a 400 KB Unicode line.
Git contents and the index remain unchanged.

All twelve regression runners pass: bookmarks, source origin, pointer gestures,
caret rendering, wrapped text, line navigation, anchors, focus return, shortcut
ownership, local context, the seven navigation journeys, and large-source
rendering. The thirteen anchor comparisons still pass.

The first native replay caught stale copy endpoints after collapse and Command
being interpreted as a word modifier. The handler now follows the app's existing
Command/Control-compatible input convention, documented in
`docs/afterhours-gaps.md`. Collapsed selections clear copy output. The next run
reached the correct file boundaries; its remaining failure was an assertion that
ignored the menu label's leading spacing. Review also found that unchanged diff
rows need side-specific line numbers. The final mapping and copied-location
checks cover both sides explicitly. Failed runs are retained.

Across the original and shifted-line fixtures, p99 render times range from 1.00
to 8.26 ms. The largest single frame is 19.50 ms. Source runs end on the large
Unicode line; separate large-source regressions also pass the 20 ms p99 gate.
These measurements do not replace the final cold/warm navigation measurements or
resolve the previously recorded warm-switch target miss.

Final binary SHA-256:
`d32c69d656e27ef42b164861d2f03ca659c68bd1c7e15dcd79c4980c536508f7`.
Baseline captures, native runs, geometry, screenshots, units, and build logs are
under `docs/reading-navigation-evidence/step46`.
