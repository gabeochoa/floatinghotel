# Step 44: caret and active line

Each document owns a logical code position with path, side, line,
and column. Pointer placement updates that position without adding a navigation
visit. Source destinations initialize it, explicit line destinations move it,
and tab activation preserves it. Rendering uses decoded columns and the same
text measurement as selection, with one caret at a wrapped-fragment boundary.

The active line has a subtle background tint, brighter line numbers, and a
separate gutter treatment. The caret remains visible while Code owns focus;
Find and other inputs keep their own focus. Empty source files display line 1.

The 38 diff/navigation unit checks pass, including wrapped Unicode positions,
empty lines, side/path isolation, and per-document caret state. An activation
test caught reuse of a transient source-line destination resetting a kept tab's
caret; the Activate operation now preserves that reading state. Nine baseline
captures cover source, unified, and split views at 100%, 140%, and 200%.
The bounded-reader suite passes 24 checks, including an empty file's first
position and invalid later positions. Reopening an empty file exposed a reader
boundary error: a targeted read of line 1, column 1 was rejected. The collector
now accepts that position and still rejects positions beyond it.

All nine native caret journeys pass at 100%, 140%, and 200%. A stale, unrendered caret retained its old
geometry in the layout dump; the runner now filters on `rendered` as well as the
clipped rectangle. At 200% the review journey explicitly reveals line 4 before
placing the caret. Without that reveal, a resize correctly preserved the earlier
reading anchor on line 1 and moved line 4 below the viewport. These failed test
attempts are retained rather than counted as successful runs.

Wrapped Unicode selection, precise line navigation, focus return, shortcut
ownership, local context, seven navigation regressions, and the 4,000-line source
reader all pass. The 13 logical-anchor comparisons differ by less than 0.001 px.
Caret placement is checked against measured text coordinates and has no flow
layout height. Pointer exit does not move it. Source header height is unchanged
when code text grows. Git contents and the index remain unchanged.

The nine caret replay p99 values are 0.83, 1.09, 1.28, 1.02, 1.76, 3.21, 2.81,
1.95, and 1.17 ms; the largest single frame is 5.82 ms. Source journeys benchmark
the final empty-file view, so the separate large-source regression provides the
content-volume gate. These results do not resolve the older 100 ms warm-switch
target miss. The bundled font still lacks glyphs for some fixture characters.

Final binary SHA-256:
`2aa4d07ee526a7c6a8388dd51d45ac32f4de9857d3cd624b5a976e8ac05b441b`.
Evidence, failed attempts, baseline captures, and build/unit logs are packaged
under `docs/reading-navigation-evidence/step44`.
