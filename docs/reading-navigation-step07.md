# Step 07: close and reopen tabs

Close buttons, Cmd+W, and tab context menus close documents. Cmd+Shift+T reopens
from a 20-document history. Closing the active document selects its nearest
neighbor; closing the final source or commit opens a visible working-changes
review. Restoring a review also retains its subject when its destination was
recreated in the meantime.

Reopening now restores the saved scroll position. A native replay caught the
old position being overwritten from a transient UI entity already reused by
the next document. Saving the last observed offset removes that race. Logical
anchors and layout readiness remain step 13 work.

The latest sidebar request is included: the duplicate “Working tree clean” /
“Working tree has changes” row is gone. History fills the space down to the
fixed footer. At 200% zoom, the Files divider previously allowed its ratio to
move past the visible header boundary. Its drag limit now respects that header,
so reversing at the limit responds immediately.

## Verification

All 38 navigation unit tests passed. The native close/reopen journey passed at
100%, 140%, and 200%, checking inactive close, nearest neighbor, keyboard and
context-menu closure, exact saved source offsets, final-tab fallback, and empty
inactive payloads. Render p99 was 3.07/1.07/1.36 ms, below the 20 ms gate.

Clean and changed repositories passed sidebar geometry checks at all three
zooms in dock and expanded windows. The six divider journeys passed, including
reversal at both limits. The existing reading-position regression and updated
dock resizing journey passed. Screenshots were inspected alongside geometry.

Binary SHA-256:
`077c8a9674f68f500e2afc9a68e5e6635fe9e733032b9315ea39cdf681872a20`.
Rerun with `tests/close_tabs.py`, `tests/sidebar_footer.py`, and
`tests/sidebar_splitter.py`, each taking `--output NEW_DIRECTORY`.
Evidence is in `docs/reading-navigation-evidence/step07`; full native runs are in
`output/step07-*-final`. The final-checks log retains the obsolete dock Back
button failure; `dock-current.log` records its corrected successful replay.

## Test corrections

The old reading-position test expected a file-tree click to restore a line deep
inside the file. Explicit file navigation targets its header; switching and
reopening documents restore reading positions. The updated test checks the file
header and its first deleted line, then checks restored commit reading position.
The new close/reopen journey checks exact source offsets independently.

The old divider replay used fixed coordinates and treated Cmd± as UI zoom.
The replacement measures the divider rectangle, uses explicit UI zoom, and
checks held pointers, direction changes, both drag limits, and footer geometry
in Review and Files at 100%, 140%, and 200%.

The dock replay now uses explicit UI zoom and closes through Cmd+W and the
existing Close working review action; its removed commit Back button is no
longer a valid target.
