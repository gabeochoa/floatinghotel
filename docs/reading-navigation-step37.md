# Step 37: one source header

Source documents use one 32-logical-pixel row for returning to review, the path,
revision, and file actions. The path ellipsizes and retains its full tooltip;
the revision shows a short object ID with the full ID in its tooltip. Narrow
readers shorten the return control. Code font settings are unchanged.

The overflow menu retains file history, bookmark add/remove, selected-line
attribution, Markdown preview, and encoding controls. Encoding has explicit
choices instead of cycling through formats. Menu actions check their originating
repository, document, and generation before applying. Returning to review ends
the source render immediately, before it can read released source state.

The first keyboard replay exposed an existing context-menu input conflict:
framework arrow navigation moved focus behind the menu, and Enter activated a
tab. The app now reserves widget navigation and activation keys before building
controls while the context menu is open. The related upstream recommendation
is recorded under U5 in `docs/afterhours-gaps.md`.

The runner `tests/source_header.py` captures normal and
narrow readers at 100%, 140%, and 200% zoom. Its `--compare` option checks the
viewport gain and unchanged code font against the same journey on the previous
binary. It exercises the moved actions and keyboard File history, and checks
focus and the exact originating review anchor on return.


The three-zoom header journey passed. The reader gained 34, 47.6, and 68 physical
pixels at 100%, 140%, and 200% zoom respectively. Code remained 17.6 logical
pixels. Both normal and narrow layouts keep every header control inside the
row. Menu dismissal preserves the rendered code positions; bookmark removal
restores the viewport height; review return restores its exact saved anchor.

The context-menu and Markdown unit suites passed all 19 checks. The final source-reader
benchmark measured 3.47, 1.36, and 1.53 ms p99 at 100%, 140%, and 200%. The
preceding run measured 1.00, 2.22, and 4.67 ms; all passed the 20 ms gate.
These are steady native headless frames, not load or compositor timings.
The final binary also passed per-document Find, all 13 logical-anchor
comparisons, focus return, all seven navigation regressions, and six independent
tabs at all three zooms. Existing bookmark, encoding, long-Markdown, renamed-file
history, and blame journeys passed. The bookmark and Markdown tests now open
sources through Quick Open; the tab test explicitly chooses working-tree scope.
Full-file Find also passed historical, UTF-16, long-line, page-boundary, and
return journeys at all three zooms.


The long-Markdown regression found a second defect. A wheel jump from the bottom
to the top updated the saved scroll offset before virtualization had produced
any visible rows at the new position. The following frame then skipped anchor
capture because the offset appeared unchanged. Resizing restored the stale
bottom anchor. The reader now advances the sampled offset only when a visible
anchor was captured or an anchor restoration applied. This leaves the new
position pending until its rows exist.


Evidence is in `docs/reading-navigation-evidence/step37`. Each archive contains
layout/workspace JSON and logs; runner-generated scripts are included where
available. Representative screenshots, including both reproduced failures, are
kept as PNGs. Extract an archive with `nice -n 10 tar -xzf`.

Rerun the new journey with `nice -n 10 python3 tests/source_header.py --output
output/source-header`. To compare viewport geometry, add `--compare` pointing
to the extracted baseline archive. The final binary SHA-256 is
`32389334752676dfd5601f45b2b587836f633c2dde4a9d910f5979376c0b80b4`.
