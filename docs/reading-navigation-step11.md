# Step 11: precise Back and Forward visits

Each history visit retains its own logical reading anchor beside its typed
destination and review context. Moving between files in one commit preserves
both locations even though they share a document tab. Ordinary scrolling
updates the current visit without adding history or changing its generation.

Back and Forward restore the visit's anchor after content and layout are ready.
A source read requests the page around that line, including bounded preceding
context. Revisiting a closed source recreates its document while keeping its
originating review. Consuming a line-reveal action clears the document's pending
request without erasing the explicit line destination from history.

History remains limited to 256 visits. Opening a new destination after going
Back removes the Forward branch. Tab closure still records the fallback visit;
reselecting the active destination remains a no-op even after a reveal was
consumed.

## Verification

All 47 navigation unit tests passed, including separate anchors for files in
one commit, closed-source origins, explicit line retention, Forward truncation,
and the 256-visit limit. The native runner
`tests/history_visits.py --output NEW_DIRECTORY` passed at 100%, 140%, and 200%.
It checks the exact wrapped fragment and viewport fraction within two pixels,
including Unicode columns 141, 173, 295, and 297. It also verifies that ordinary
scrolling and inactive-tab closure do not append visits.

All seven existing navigation regressions passed. Close/reopen and session
restart journeys passed at all three zooms. History replay p99 frame times were
2.39, 6.08, and 7.22 ms. Binary SHA-256:
`5e3ff984f06f0a1de0a0b4dc456c66caaf4cd2637143a1645c052dc764a9cab5`.

Compact screenshots, row geometry, and history snapshots are in
`docs/reading-navigation-evidence/step11`; full captures and logs are in
`output/step11-history_visits` and `output/step11-native.log`.

The first new unit run caught a no-op check suppressing the fallback visit
when closing an active tab. The check now also compares the current history
entry's document identity, while still allowing a consumed source reveal to
remain a no-op on ordinary tab activation.
