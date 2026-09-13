# Step 32: Search after a typing pause

Repository search waits for 150 ms without changes to the query or path filters.
Enter, Search, matching-mode buttons, and explicit scope changes submit immediately.
Each edit cancels the previous worker and clears its visible results before the
renderer polls for completion. Empty queries cancel pending work without running
Git. Closing the pane preserves pending input; reopening resumes it. Completed
results remain available without another search.

Scheduling uses the steady clock. The native runner's readiness gate includes
pending searches, so accelerated headless frames do not skip the typing pause.
`FH_TRACE_READING=1` records the submission reason and measured delay. The existing
worker cancellation, foreground capacity, 5,000-match cap, and 4 MiB output cap
are unchanged.

Verification passed. `tests/search_debounce.py` drives rapid typing,
immediate Enter, a two-second delayed Git worker, automatic path filtering,
empty queries, close/reopen, and capped results at three zoom levels. Its Git
wrapper records process IDs and verifies the superseded process exits before
its delay ends, independently from the UI. The first test incorrectly expected
SIGTERM; the existing bounded process runner uses SIGKILL for cancelled reads.

Automatic submission delays measured 150.03–151.38 ms. Enter submitted within
1.18–3.21 ms in this replay. Render p99 was 1.51, 2.28, and 3.99 ms at 100%,
140%, and 200%. These are local fixture measurements, not cold-search latency.
The delayed read was cancelled before starting on one faster run; the test
accepts both queued and running cancellation and requires a running cancellation
in the three-zoom replay.

All ten search unit tests, three-zoom retained-pane and comparison-origin
journeys, seven navigation regressions, and three search-filter regressions pass.
Twelve screenshot geometry checks confirm the input, status, and results remain
visible; the 5,000-result case creates fewer than 50 rendered result rows. Git
status remains unchanged. Evidence is in `docs/reading-navigation-evidence/step32`.
