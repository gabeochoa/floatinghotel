# Step 26: stationary history pagination

History requests another 100 commits when the last row enters the virtual
list's window. Pages use the displayed HEAD object ID, append to existing rows,
and leave the active review unchanged. Only one page request runs at a time;
a history refresh cancels it. Failed reads keep the existing list and offer
an inline retry. The loading row disappears at the repository's first commit.

Source inspection found that the earlier loading row had no pagination request
behind it. The new replay creates 240 commits, injects a failed page read,
retries, then reaches the root commit. It compares visible-row coordinates
before and after both appends and records the actual Git page arguments.

Initial mouse and keyboard retry journeys passed at all three zoom levels,
with zero measured vertical movement across both appends. A follow-up capture
showed focus falling back to the repository tab when a successful retry removed
the loading row. The sidebar now restores history focus before rebuilding the
list, without changing the selected review. The replay checks Up/Down immediately
after retry. The updated pagination, commit-keyboard, and focus-return journeys pass at
all three zooms. The earlier refresh-position and seven navigation regressions
also pass. The final label uses primary text and measures 6.37:1 contrast. Its text fits
at every zoom. The final acceptance replay passes with zero measured row movement.
Frame p99 samples are 15.93, 5.15, and 5.60 ms at 100%, 140%, and 200%; the
100% maximum was 26.96 ms. Earlier samples were 4.50, 5.76, and 4.00 ms p99;
these runs remain available rather than averaging away the variability.

Evidence: `docs/reading-navigation-evidence/step26` (raw JSON is gzip-compressed).
Final replay: `output/step26-acceptance`. Regression runs before the final text-color
change are `output/step26-verified-*` and `output/step26-final-*`.
Binary SHA-256: `c94423d9f79176f715d2cb4fc3266fe1778602c7eef636ac7742f6c5e1d8228b`.
