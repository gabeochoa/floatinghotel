# Step 52 — repeated selection preserves the active read

Implemented and verified.

Opening the active document without a new file or line target retains its exact
location. Reselecting a commit keeps the selected diff file. Repeated source
selection no longer clears a selection while its initial line request is still
pending. Explicit file and line destinations continue to navigate normally.

The navigation boundary performs this normalization before workspace history or
request generations change. Existing caches and refresh invalidation remain in
place. No new retention cache is introduced.

The before-change hidden-native replay lost the selected diff file after clicking
the same commit again. Its source probe also expected the wrong revision: opening
a removed diff line correctly selected the parent version. The corrected runner
opens the commit source explicitly through Quick Open. All 65 navigation unit
checks pass, including request-stamp, selection, caret, and history preservation.
The first corrected native run passed all three review counter checks. Its
source probe revealed a stale test expectation left behind by the compact source
header: the probe expected a combined path/revision label. It now checks the
rendered path label while independently matching the selected revision. Source
history, caret, and selection already matched in that run.

Final binary: `5b5d7490e56690fd126084072218d900b6182dc58268a45ef6937d2cb6f24e1c`.
All twelve repeated-selection cases pass at 100%, 140%, and 200% in hidden
native windows. Patch and blob hit/miss deltas remain zero; history, caret,
selection, and active document remain unchanged. Frame p99 is 3.30–3.86 ms,
maximum 9.09 ms. All seven navigation regressions and the ownership checker
pass. Evidence is in `docs/reading-navigation-evidence/step52`.
