# Step 33: Grouped search matches

Search results have collapsible file headers with counts and separate directory
labels. Group identity includes the full path and revision. Each match row shows
its source line and highlighted code. Groups and their collapsed state remain
with retained results when navigating documents or closing/reopening search.

The worker computes matching spans using POSIX extended expressions, literal
escaping, case and whole-word options, and a thread-local environment locale.
Highlighting does not run in the render loop. Long-line excerpts start near the
first match and preserve UTF-8 boundaries. A fixed 512-bit mask stores highlighted
bytes without copying an extra excerpt string: 320,000 mask bytes for 5,000 hits.
The original line stays available for source-change checks and nearby previews.

A flattened header/match list feeds the existing virtual-list adapter. Rebuilding
happens when results arrive or a group is toggled, not during every frame.
Collapsing a group preserves its matches and does not resubmit the query.

The first native capture showed that 80 leading bytes could push a long-line
match outside the sidebar. Excerpts now retain eight leading bytes. Font-rendered
disclosure triangles were absent; the headers now use existing drawn chevrons.
The first geometry assertion also treated virtual-list overscan rows as visible;
the corrected check allows clipped overscan while bounding total generated rows.
A follow-up capture found the drawn chevron overlapping styled button text.
Separate icon and label children now reserve layout space; the native test checks
their geometry. Japanese text remains intact in result data and unit-tested
highlight offsets, but Roboto does not draw its glyphs. That existing font
fallback limitation is recorded in `docs/afterhours-gaps.md`; Unicode glyph
coverage is not claimed by this step. The cancellation regression also exposed a fixed-frame startup race. Its new
`wait_for_path` command waits for a worker-owned start marker using a steady-clock
deadline before replacing the query. The corrected cancellation replay passes at all three zoom levels.

The grouped native replay passes at 100%, 140%, and 200%, including collapse,
source selection under duplicate basenames, retained collapse state, visible
highlight pixels, and chevron/label geometry. The 5,000-result fixture retains
5,002 logical rows but creates fewer than 60 result/header entities. Mask storage
is 320,000 bytes and row-vector capacity is 196,608 bytes; these figures exclude
original result strings and group metadata.

With all 5,000 results loaded, render p99 was 3.77, 3.20, and 3.32 ms. After
opening source and searching the long-line fixture, p99 was 6.79, 12.59, and
4.25 ms. All are below the 20 ms gate; the largest frame was 13.94 ms. The twelve
search unit tests pass. Three-zoom debounce, retained-pane, and comparison-origin regressions pass, as
do all seven navigation regressions and three search-filter journeys. The older
filter assertions now use group identities and exact source positions because the
framework text registry records highlight spans separately. Git status remains
unchanged. Evidence, including failed test assumptions, is in
`docs/reading-navigation-evidence/step33`.
