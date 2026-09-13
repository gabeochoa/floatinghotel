# Step 51 — conservative source folding

Implemented and verified.

Source files have gutter fold controls and Fold block at caret / Unfold all
header actions. Folding preserves original source positions, syntax state, and
read-only contents. Closing brace lines remain visible. Each document keeps its
own collapsed ranges; a new source identity resets them. Direct line navigation,
Back, and caret movement reveal hidden destinations.

The lexer proves balanced brace ranges for C/C++, Objective-C, JavaScript,
TypeScript, and JSON. Python folds require a recognized suite header, greater
indentation, and a dedent or confirmed end of file. Only ranges established from
the bounded loaded window are offered. Ambiguous JavaScript slash expressions
and template interpolation stop further discovery, while earlier proven ranges
remain available. Python tab indentation is left unfolded. This is deliberately
conservative and does not introduce a language server or semantic parser.

Collapsed ranges contain only line coordinates and a source identity. There are
at most 512 per document; controls explain the limit and still allow unfolding.
Only the active reader retains discovered ranges. Existing cache budgets remain
unchanged. The cumulative wrapping index still describes original source lines;
folded intervals skip their rows without changing content identities.

The initial native journey passed at 100%, 140%, and 200% for nested folds,
Python, direct reveal, tab return, reopen, resizing, and gutter geometry. The
18,000-line fixture keeps over 1,000 discovered ranges with fewer than 300
rendered code rows. Its first 200% run missed the frame gates at p99 36.65 and
54.26 ms (maximum 231.01 ms), despite a 3–4 ms median. The unchanged 200% replay
passed at p99 3.11–7.16 ms. Both runs are retained; no cause is claimed for the
transient timing spikes.

Two verification failures changed the implementation. Escaped CRLF strings
initially exposed braces inside a continued literal; folding now uses the same
line lookahead as syntax scanning. A fast fold followed by close/reopen at 200%
also exposed input overlap: the disclosure click started a code selection drag,
whose edge scrolling cancelled the anchor. The disclosure gutter is excluded
from code selection. Fold generations additionally reject stale layout
acknowledgements. The unchanged fast replay remains the acceptance check.

Final binary: `14746ea32ffdbc7c112c511fba26a4b3b996108e638104de7333a78aa9cf5e6b`.

The corrected fast fold/close/reopen replay passes unchanged at 200%. Final
hidden-native and screenshot journeys pass at all three zooms, including Find
reveal and font-only resizing. Native p99 is 2.96–4.91 ms. The large-file replay
passes all nine frame gates at p99 4.56–10.77 ms, maximum 25.62 ms. All seven
navigation regressions pass. Targeted unit suites contain 160 passing checks.

The broader anchor replay missed its gate at p99 20.06 ms (maximum 30.58 ms).
The final 200% split selection replay missed at p99 27.80 ms; the first eight
selection cases passed. The unchanged anchor repeat passed at p99 6.51 ms,
with all thirteen anchor errors below 0.005 pixels. Both final split-selection
gates passed at p99 8.80 and 12.81 ms. Failed runs remain in the evidence.

Screenshots, scripts, geometry, logs, and before-change evidence are packaged in
`docs/reading-navigation-evidence/step51`. Existing syntax, wrapping, and Unicode
fragment replays also passed during this step. The later pointer-only fix was
verified with selection, anchor, navigation, and folding replays on the final
binary. Physical live-resize timing remains outside these injected journeys.
