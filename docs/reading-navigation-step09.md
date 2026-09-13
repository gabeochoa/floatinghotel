# Step 09: recent document switcher

Hold Ctrl and press Tab to select the most recently used document. Further Tab
presses traverse a fixed MRU list; Shift reverses direction. Releasing Ctrl
opens the highlighted document through the navigation boundary. Escape cancels.
The reader remains visible at its original position while choosing.

The overlay renders at most eight rows and reveals the highlighted row. It
stays inside narrow windows at increased zoom. Closed documents are removed
from the in-flight list, newly opened documents wait for the next invocation,
and switching repositories dismisses the overlay. Repository tabs are excluded.
Tab reordering does not change MRU order.

The native test driver now supports explicit held/released physical key codes,
so the replay observes the modifier-held interval as well as the release.
The runner is `tests/recent_tabs.py --output NEW_DIRECTORY`.

## Verification

Two cycle tests and 40 navigation tests passed. Native journeys passed at
100%, 140%, and 200%, including both Ctrl keys, ordinary Tab, single-document
behavior, forward/reverse cycling, modifier release, Escape, closed candidates,
repository changes, and narrow-window geometry. Pixel checks verify selection,
and reader offsets stay unchanged while choosing. The preview/keep,
close/reopen, and reorder regressions also passed at all three zooms.

Binary SHA-256:
`0244173f0b2d25654e4c6d5a706053a786643d256dec78c865356c9463716144`.
Evidence is in `docs/reading-navigation-evidence/step09`; full captures are in
`output/step09-final` and the named regression directories.

The first compile used an incorrect namespace for UIContext. The first replay
passed keyboard behavior at 100%; screenshot inspection at 140% caught the
framework's default proportional corner radius on the panel. Panel and row
corners are now explicit. The selected-row pixel assertion samples inside the
row rather than its rounded corner.
