# Step 35: Per-document Find overlay

Find now belongs to its document. Switching tabs or closing and reopening a tab
preserves its query and selected source location. A selected single line seeds
the query, including a selection spanning wrapped fragments. Find opens above
the reader without changing the code viewport's size or position.

Next, Previous, Enter, and Shift+Enter use logical line/column anchors through
the shared navigation boundary. Explicit match navigation unfolds the matching
file and hunk. Opening an existing query does not navigate. Escape and the close
button restore focus to the code viewport, identified by its semantic region
and viewport item.

The native replay found two focus-restoration failures: an unnamed decorative
child inherited a matching region identity, and a focusable hunk header could
substitute for the viewport. Restoration now requires an eligible control and
uses an explicit viewport identity. Both failed captures are retained. Test
corrections also account for asynchronous Quick Open readiness, match indices
changing when a different source page is loaded, and source revision selection
when opening a deleted diff line. Escape comparisons now start after the query's
explicit navigation and compare document identity separately from Find state.

Verification passed:

- 48 unit checks covering Find state, navigation, sessions, focus, and tab cycling.
- Per-document query/match restoration, single-line seeding, keyboard traversal,
  close focus, and identical code-row geometry at 100%, 140%, and 200%, including
  narrow windows.
- Existing focus, shortcut, Escape, unified/split/source selection, and wrapped
  Unicode selection journeys at all three zooms.
- 13 logical-anchor layout comparisons and all seven navigation regressions.

The final Find journey measured p99 rendering at 1.21, 2.33, and 0.89 ms at
100%, 140%, and 200%; its largest frame was 3.05 ms. All rendering gates passed.
These are headless native measurements, not physical mouse/compositor timing.
Source Find still scans the loaded page in this step; complete-file searching
is step 36. Unicode text and selection bytes are verified; the bundled fonts'
Japanese glyph coverage limitation recorded in step 33 remains open.

Evidence: `docs/reading-navigation-evidence/step35`. The runner is
`nice -n 10 python3 tests/document_find.py --output output/find-replay`.
