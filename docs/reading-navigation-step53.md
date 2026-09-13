# Step 53 — immediate identity, delayed busy feedback

Implemented and verified.

Commit and source destinations keep their immediate headings. Busy text waits
150 ms from request dispatch. Comparison and untracked-file loading messages
use the same timing rule. A replacement request starts its own delay; completed
requests never retain busy text.

Pending source pages clear their derived fold controls along with their rendered
payload. A comparison being replaced does not render its previous diff beneath
the submitted revision fields. Existing repository/document/request/generation
checks still control publication. No old-content retention cache is added.

The clock-boundary tests and all 65 navigation unit checks pass. The refresh
lifetime test initially failed to link because its runner omitted the existing
untracked-file reader; the corrected target passes both cancellation and live-tab
checks. Inspection caught a missing brace around untracked-read dispatch before
native verification; that build was stopped and the guarded dispatch rebuilt.

Final binary: `407c2c848277a826d8384f0296dc5799cf4217644c46597ed5d96206fa2db3f8`.
All 69 targeted unit checks pass. Delayed commit, source, and comparison journeys
pass at 100%, 140%, and 200% in hidden native windows and offscreen screenshots.
The requested title is present while old reading rows are absent. Each
superseded child process is cancelled, and the destination remains correct after
settling. Busy messages fit inside the reading pane; the source header remains
32 logical pixels tall. Native p99 is 4.78–6.00 ms, maximum 8.38 ms. Screenshot
p99 is 1.59–8.84 ms. Twelve repeated-selection regressions also pass with zero
additional cache reads.

The preserved step 52 binary shows busy text in the initial commit/source
snapshots. The final binary has the requested heading without busy text in all
six initial native snapshots, then shows busy text after the explicit delayed
read marker. Exact 149/150 ms boundaries are tested with the injected clock.
These checks do not claim a measured p95 selection latency; the full acceptance
replay remains responsible for that measurement.

Two initial replay failures were in the harness: its Git wrapper missed literal
path arguments, and it assumed preview tabs kept their IDs. Another attempt
pressed Enter before Quick Open results were ready. The final runner keeps the
reference source open and cancels via its tab. Failed evidence is retained.
Screenshots, geometry, scripts, and logs are in
`docs/reading-navigation-evidence/step53`.
