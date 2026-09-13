# Step 36: Find across the complete source file

Source Find scans bounded pages on a worker and retains at most 5,000 matches.
Selecting a match loads its line and column, including a position beyond the
first 256 KiB of a very long line. Historical and index destinations retain their
source identity. UTF-16 decoding, line fragments, and cross-page matches use the
same reader as the displayed file. A visible notice identifies the result limit;
queries above 64 KiB report an error instead of starting an unbounded scan.

Results apply only to the matching repository, document, query, generation, and
source identity. Changing the query cancels its old read. Enter or Previous/Next
pressed while results are loading is retained and applied when results arrive.
Inactive documents retain their query and match position, not the result vector.
The 5,000-match fixture retains 64 KiB of result storage, including vector spare
capacity. Existing cache budgets are unchanged.

The first long-line replay failed the render gate: p99 was 106.16 ms while
showing a 256 KiB page. The app rebuilt a whole line's display string and prefix
for every visible wrapped fragment. It now prepares that text once per line,
reuses the existing token cache, and advances display-byte and decoded-column
offsets across fragments. Anchor projection also computes its target byte once.
The final full-page replay measured 5.49, 3.17, and 4.94 ms p99 at 100%, 140%,
and 200%; its largest frame was 5.89 ms. These are native headless rendering
measurements. Worker file-scan latency and physical compositor timing are not
included in those numbers.

Verification passed:

- 70 unit checks covering complete-file Find, bounded reads, source positions,
  decoding, long-line boundaries, result limits, sessions, and navigation.
- Three-zoom native journeys through historical lines 1, 4,500, and 6,200;
  UTF-16 line 5,000; the 5,000-match notice; long-line columns; tab return;
  pending Enter; and a full 256 KiB source page.
- Three-zoom delayed-read tests that wait for a running Git worker, replace its
  query, verify that its process exits, and confirm only the newer query renders.
- Existing per-document Find, wrapped and plain selection, focus-return,
  13 logical-anchor comparisons, and all seven navigation regressions.

The original performance failure and the ignored-Enter failure are retained.
The tab-return test uses tab activation; explicitly opening a path through Quick
Open is a separate navigation operation. Japanese glyph fallback remains the
open visual limitation recorded in step 33; decoded text and match positions
are verified here.

Evidence is under `docs/reading-navigation-evidence/step36`. Each runner's
`.tar.gz` contains its scripts, layout/workspace JSON, and logs; representative
PNGs remain directly viewable. Extract an archive with `nice -n 10 tar -xzf`.
This avoids adding hundreds of separate evidence files to a code-review commit.
Rerun with `nice -n 10 python3 tests/source_find.py --output output/find-full`
and `nice -n 10 python3 tests/source_find_cancel.py --output output/find-cancel`.
