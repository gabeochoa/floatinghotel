# Step 49: continuous source scrolling

Completed and verified.

The active reader owns one raw-content window with up to three page-boundary
records. Each read keeps the existing 256 KiB and 4,096-line limits. Loading near
an edge joins adjacent bytes and evicts the opposite page before extending the
window. Decoding the joined bytes preserves Unicode, CRLF, and lines split across
page boundaries. Version, offsets, line/column positions, encoding, and blob
identity must match before joining. Inactive documents retain no window.

Existing content stays visible while an adjacent page loads. Publication waits
for layout to record the current reading anchor, then restores that anchor in
the joined window. Normal source text no longer has manual page controls. Binary
and rendered-Markdown views retain them. Reload file is available from the source
header menu when a working file changes between reads.

## Verification

- Nine hidden native journeys pass at 100%, 140%, and 200%: working-tree, index,
  and immutable historical versions of a 26,000-line file. Checks cover both
  directions, eviction, exact visible text, revision identity, delayed reads,
  anchor geometry, font/viewport changes, tab return, and closing during a read.
- Three native long-line journeys pass with a 1.2 MB Unicode line, CRLF, and a
  final line without a newline. They verify fragment columns, exact bytes,
  absence of duplicate rows, eviction, EOF, and returning to the first page.
- 61 unit checks pass across source windows, file-page accounting, diff tools,
  metrics, and token caching. Cases include invalid joins, overlapping pages,
  changed working files, limits, empty files, cache invalidation, and eviction.
- Existing complete-source Find, Find cancellation, line navigation, page and
  layout anchors, local context, wrapping, fifteen keyboard-selection cases,
  eighteen selection-extent cases, and 8 MiB copy refusal/cancellation pass.
  Seven navigation scenarios pass. The final rendering change is also replayed
  through wrapping and navigation on the final executable. The four updated
  legacy paging/search scenarios pass; the large-review main journey and its
  corrected search-cap companion pass separately. Six final screenshot journeys
  pass with geometry checks at all three zooms.

The final native source-window benchmarks have p99 2.33–14.89 ms across 27
measurements, with a maximum frame of 32.20 ms. Long-fragment benchmarks have
p99 4.39–13.75 ms, maximum 21.69 ms. All final feature gates retain the existing
20 ms p99 threshold. These results do not imply that every frame is below 16 ms.

The line-limited fixture retains 307,200–307,224 raw bytes in three pages, with
308,010–308,101 bytes of owned window storage and boundary metadata. The long-line
fixture reaches the three-page byte limit: 786,432 raw bytes and 787,234 owned
window bytes. These window figures exclude parsed rows and the existing caches;
they are not process-memory measurements. Cache budgets are unchanged.

The final executable SHA-256 is
`1190af6c09dca9c4d630ef5b3190739ba042db4ecbfcb014340c3a1fd440dc18`.

## Corrections and retained failures

The first two native runs restored an old anchor after scrolling. One cause was
publication before layout sampled the new scroll position. The other was
float32 spacer accumulation: grouping 8,192 rows of 25.6 pixels differently
changed the total by about 21 pixels. Double-precision accumulation reduces that
numeric example below one pixel. Source reflow now depends on content identity,
font/whitespace mode, and viewport geometry, instead of rounded virtual extents.

Three-page traversal also exposed unnecessary per-frame work. A native profile
showed allocations in wrapping-cache key construction. Keys now encode fixed
metric fields without repeated numeric formatting. The reader caches cumulative
wrapped-row counts in the existing 3 MiB metrics cache, allowing offscreen source
lines to skip gutter and wrapping preparation while preserving explicit anchor
and Find destinations. A long-line profile then found display-text conversion
before token-cache hits and review-hunk hashing in source-only views. Token lookup
now uses published line identity, language, and whitespace mode before converting
text; source-only hunks skip review-key hashing. The token budget remains 4 MiB.

Earlier runs are retained, including source-window p99 misses of 21.35 ms,
25.51 ms, and a 105.99 ms outlier; later intermediate runs still missed at
20.45–25.10 ms. Long-fragment reruns missed at 21.80 and 20.58 ms before the last
correction. A separate split-selection run missed at 28.44 ms; its repeated
cases passed at 9.49–13.32 ms. Profile runs include sampling overhead and are
not acceptance measurements. An interrupted regression batch overlapped a build
and is explicitly excluded from timing acceptance.

Legacy tests were updated for current behavior: source opening follows the
selected diff side, Quick Open preserves its query, new pages preserve the
viewport, and dismissing search previews retains the reader. Historical cache
reuse is tested by closing and reopening the source. The EOF geometry assertion
allows a half-pixel tolerance after a 0.00625-pixel rounding difference caused a
false failure. Earlier legacy review timing misses, including 20.06 ms, remain
in the logs.

## Evidence and limits

`tests/continuous_source.py` and `tests/continuous_fragments.py` reproduce the
native journeys; `--snapshots` captures their offscreen images and layout dumps.
They report behavior separately from timing and return failure if a frame gate
misses. Build logs, failed and passing journeys, profiles, geometry, and selected
screenshots are packaged under `docs/reading-navigation-evidence/step49`.

Native tests use hidden Cocoa/Metal windows. Screenshots are offscreen renders;
the source and long-fragment views were also visually inspected. These checks
do not establish physical live-resize latency or fix the plan's separate warm
document-switch target. Framework candidates and workarounds are recorded in
`docs/afterhours-gaps.md`.

The evidence package contains 140 verified-readable artifacts (49,108,679 bytes),
including seven selected PNGs. Final images are `source-100.png`,
`source-140.png`, `source-200.png`, and the corresponding `fragment-*.png` files;
`before-page-controls.png` records the previous explicit paging UI.
