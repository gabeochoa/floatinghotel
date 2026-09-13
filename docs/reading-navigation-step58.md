# Step 58: follow-up review itinerary

Since last review includes an on-demand Follow-up list. It combines files
changed since the saved baseline with unresolved working-review comment
locations. Identical locations are grouped, resolved unchanged work is omitted,
and changed-file entries merge with their comment locations. Renamed old-side
comments retain the current diff-file identity.

Current or uniquely relocated comments open their line. Outdated, ambiguous,
and unlocated entries carry explicit labels and open the file with feedback
visible, without guessing a line. The existing menu handles keyboard navigation,
scrolling, and rendering only visible rows. The list is built when opened.

The isolated persistence path correction and fixture cleanup are recorded in
`docs/afterhours-gaps.md` and `output/step58-fixture-cleanup.json`.

Final binary: `90845fab3c11ab17682c343dce188b4ccc06aed2373e9d2edbddca303a1368eb`.

All 24 itinerary/store unit checks pass: duplicate and resolved comments,
changed-file merging, renamed old sides, deterministic large lists, and current,
relocated, outdated, ambiguous, and unlocated anchors. Native and offscreen
journeys pass at 100%, 140%, and 200%, including exact comment-line navigation,
visible warnings, unchanged baseline/index/working bytes, and a 2,000-location
menu with fewer than 50 rendered item entities. Keyboard Up reaches its last
item from the initial state. The list is benchmarked both open and closed.

Final native p99 samples were 3.54–12.80 ms; offscreen p99 was 1.67–5.78 ms.
One earlier bulk run missed the gate at 44.93 ms p99, 98.94 ms maximum. An
unchanged repeat passed, and final runs include the additional open-list gate.
The timing miss is retained. An interrupted fixture run queried HEAD once per
comment; the fixture now resolves it once.

The saved-baseline restart/return and Escape regressions pass at all zooms.
Loaded review paths are asserted to remain inside each isolated settings root.
Screenshots were inspected for menu geometry, warnings, and navigation.
Evidence: 25 artifacts, 3,661,248 bytes; compressed members and
PNG signatures verified in `docs/reading-navigation-evidence/step58`.
