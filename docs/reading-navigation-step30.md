# Step 30: Direct line and column navigation

Quick Open accepts `path:line[:column]`. Exact filenames, including filenames
containing colons, take precedence. An exact path before the final suffix also
wins over interpreting that suffix as a column. Line and column values must be
positive integers within the supported integer range.

A bounded worker validates source positions before navigation. Invalid or
out-of-file lines leave the current document and its history unchanged and
show an error in the picker. Columns beyond a complete line clamp to its end.
A column beyond an incomplete long-line fragment gets an explicit error.
Historical destinations retain their resolved object ID. Superseded queries,
repository changes, and dismissal cancel pending validation; results require a
matching navigation request stamp before application.

Ctrl+G and Go to Line open a compact overlay for the current source or selected
review file. Review positions use the current diff side, preserve the review
document, and unfold only the target file and hunk. Lines outside the displayed
diff are rejected with a route to Go to File. Successful jumps use logical
anchors and add precise Back/Forward visits. Existing cache budgets remain.

The 30 diff-tool, 59 navigation, and one real-Git source-position tests pass.
The source-position test covers working-tree, index, immutable, missing,
cancelled, binary, CRLF/Unicode, later-page, and explicit UTF-16LE reads.

Final binary: `a62ca2ddee2750d92f7ea23a237ce1ce5925cd213461e8da5d9c7e471bde2674`.
The native line, history, focus, and shortcut journeys pass at 100%, 140%, and
200%, along with all seven navigation regressions and the navigation ownership
checker. All 13 zoom, resize, split/unified, folding, and Markdown anchor
comparisons pass within 0.001 pixels. The line journey checks literal colon
paths, line/column bounds, unchanged documents on errors, wrapped columns,
later pages, historical files, deleted sides, superseded reads, and narrow
picker geometry. It verifies that the fixture's Git status does not change.

Line-navigation p99 is 3.83 / 3.60 / 2.15 ms. History p99 is
4.82 / 3.99 / 1.65 ms. Focus p99 is 7.14 / 3.37 / 3.06 ms, with a 47.50 ms
maximum frame at 100%. Earlier line runs ranged from 3.80 to 11.32 ms p99;
these results are retained rather than averaged. The existing 20 ms p99 gates
pass. This does not establish the separate warm-switch latency target.

The first native replay found that navigation tried to focus the document tab
before the closing picker's input gate was removed. Document focus now resolves
in the existing semantic focus phase after layout and modal teardown. Screenshot
review also replaced an unreadable empty-field placeholder with a readable
format instruction. Preview and MRU journeys pass at all three zooms on the
preceding build; the subsequent change only fixes wrapped-anchor restoration.

Older preview/history helpers needed explicit historical-catalog readiness.
The preview journey now explicitly chooses working-tree scope for files absent
from its selected commit and expects history to retain focus, as specified in
step 25. One line-test assertion conflated the requested EOF line with the first
visible line; it now checks the destination separately from the viewport anchor.

The closed-source history replay exposed a one-row restoration error at a
wrapped UTF-8 column boundary. Adjacent fragments both claimed the same column.
Preferring the greatest measured or registered starting column did not fix it:
the trace showed that the destination fragment could be absent from the
virtualized row registry. Non-final fragments now exclude their end column;
only the final fragment accepts the end-of-line position. The exact target must
have measured geometry before restoration completes. Nearest-line fallback
applies only when the requested line is absent, not when its fragment is pending.
There is no fixed frame retry count. `FH_TRACE_READING=1` logs applied anchors;
layout dumps include logical columns and final-fragment flags.

Screenshots, compressed layouts, unit results, failed attempts, diagnostic
traces, and regression logs are in `docs/reading-navigation-evidence/step30`.
Full outputs remain under `output/step30-*`. The focus and text-position
projection upstream gaps are recorded in `docs/afterhours-gaps.md`.
