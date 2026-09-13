# Step 08: drag document tabs

Drag a tab past six logical pixels to reorder it. A two-pixel insertion marker
shows the destination. Holding near either edge scrolls the strip, with speed
bounded by elapsed time. Release inside the strip to apply the move; Escape,
release outside, a changed document list, repository change, resize, or zoom
change cancels it.

Order changes go through `navigation::reorder` and move the existing document
values. They do not select another document, append history, change request
generation, reload content, or change preview/kept status. Tabs activate on
release so starting a drag does not select the dragged document first. Once the
threshold is crossed, the strip captures the pointer until release. Close
controls, tooltips, and close hover highlights are suppressed during the drag.

## Verification

Six strip geometry tests, 39 navigation tests, and the navigation ownership
checker passed. The final native reorder journey passed at all three zooms,
including dragging an inactive tab and preserving the active request. Render
p99 was 1.88/1.11/1.86 ms, below the existing 20 ms gate. The preview/keep,
close/reopen, 25-tab overflow, and hover regressions also passed at all three
zooms. Screenshot inspection confirmed the insertion marker and removal of
unrelated tooltips during dragging.

Binary SHA-256:
`5a68e80c46b230b72779e219c54eed4da72d604118c7101d3b8c58cbd538214c`.
Evidence is in `docs/reading-navigation-evidence/step08`; full captures are in
`output/step08-final` and the regression directories named in its evidence log.

The repeatable runner is `tests/reorder_tabs.py --output NEW_DIRECTORY`. It
creates eight source files and tests both reorder directions, both scrolling
edges, marker geometry/pixels, Escape while held, outside release, release over
close, sub-threshold movement, resize cancellation, stable document identities,
and unchanged reader offsets at 100%, 140%, and 200%.

`--resume` rechecks completed captures and runs missing journeys only when the
binary hash and generated script match. The initial replay's marker check used
the wrong expected accent color; the actual pixels matched the configured
color. The corrected check reused those captures before continuing.
