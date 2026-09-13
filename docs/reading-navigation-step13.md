# Step 13: logical reading anchors after layout

The reader now restores a path, revision, side, line, decoded character column,
and viewport fraction against measured row bounds. It no longer retains a map
of pixel offsets or retries that pixel restoration for three frames. Each layout
request must still match the repository, document identity, request key,
navigation generation, and data generation.

Zoom, font changes, resizing, unified/split presentation, and folds request a new
layout for the saved position. Collapsed files and hunks project the position to
their marker without replacing the underlying code anchor. Markdown preview
retains source positions through fenced code and wrapped paragraphs. Explicit
scrolling cancels pending restoration and records the new visible position.
Tab activation, close/reopen, and Back/Forward use the same document anchor.
Only the active reader retains rendering data.

The virtual range is chosen around the requested line before applying viewport
bounds. The post-layout system then clamps the offset to the measured content.
A missing-line fallback must match the nearest line in the document, rather
than whichever row happened to be rendered. The reader disables the framework's
child-entity scroll anchor because virtualized entities do not identify source
positions. Explicit page changes discard the previous page's anchor and data
before the next frame can sample it again.

## Verification

All checks below passed on the final binary. The 13 layout comparisons were
within 0.001 physical pixels of the saved viewport fraction.

- `tests/anchor_layout.py`: zoom at 100%, 140%, and 200%, narrow resizing, font
  changes, source activation, unified/split changes, file folds, Markdown
  reflow, and explicit scroll cancellation, with screenshots and geometry.
- `tests/anchor_pages.py`: page two of an 8,000-line source, scrolling, releasing
  the active payload, then activating the saved line at all three zooms.
- Navigation, source-origin, precise-history, restored-tab, close/reopen,
  large-source wrapping, and window restoration regressions.
- 53 navigation unit tests, five Markdown tests, and navigation ownership checks.

The initial before-change replay is in `output/step13-baseline`. Zoom displaced
the saved code point by 638.94 pixels, tab activation by 58 pixels, and split
presentation by 1,302 pixels. Narrow/font/split-zoom cases did not render the
saved point. These are individual failures, not average timing measurements.

Intermediate failures are retained. `output/step13-source_origin` and
`output/step13-trace` show the six-line error caused by clamping the virtual
range to an empty prior layout. `output/step13-pages-before` and
`output/step13-pages-fixed` show a stale page-one anchor sampled after the user
requested page two. The implementation fixes both at their source.

The close/reopen test previously required identical pixel offsets. Bounded
source reads can start on a later line, so a different offset can preserve the
same code position. Its old assertion failed at `output/step13-accept-close_tabs`;
the saved source line was within 0.001 pixels of its intended viewport fraction.
The test now asserts that line and fraction, as the navigation contract requires.

Source-origin frame p99 values were 3.27, 5.87, and 3.07 ms at 100%, 140%, and
200%. Page restoration measured 5.62, 6.86, and 6.25 ms. Session restart measured
12.43, 16.78, and 10.04 ms. The large-source replay measured 7.54 ms. All stayed
below the existing 20 ms gate. This step does not claim the separate 100 ms
cache-resident document-switch target is solved.

Binary SHA-256:
`784820a2b239079da5a14eb6b8bc76d5c533dcd518ba870f3852197aa38b6357`.
Screenshots, compact geometry, logical anchors, baseline errors, unit logs,
and frame measurements are in `docs/reading-navigation-evidence/step13`.
Full captures are under `output/step13-verified-anchor_layout`,
`output/step13-accept-*`, and `output/step13-checked-*`. The older pixel assertion
failure remains in the recorded log; its replacement passed at all three zooms.
