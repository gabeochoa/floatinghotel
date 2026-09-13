# Step 50 — syntax state across lines and pages

Implementation and verification complete.

The bounded source reader carries lexical state in each page cursor. Forward reads
continue that state; direct and backward reads derive it from the revision's prefix.
The renderer uses each logical line's incoming state, and token-cache keys include
that state. The existing blob, patch, metrics, and token budgets remain unchanged.

The lexer recognizes multiline block comments, quoted strings and escaped newlines,
C++ raw strings, Python triple quotes, and JavaScript/TypeScript template strings.
Objective-C uses C rules; Objective-C++ uses C++ rules. This remains a lightweight
lexer, without semantic interpolation or regular-expression parsing.

Diff hunks keep separate before and after state. A cancellable background read
obtains state from the omitted prefix of each revision; publication checks the
repository, document, request, generation, and file identity. Renamed files use
the old and new paths independently. Local context reads retain their prefix state.
Opening a review does not wait for syntax reads.

The initial screenshot replay reproduced incorrect comment/string colors at 100%,
140%, and 200%. Evidence is in `output/step50-baseline`. The native source replay passes for working-tree, index, and historical files at
all three zooms. Nine rolling-window journeys also pass with delayed reads,
three-page eviction, backward traversal, exact text, and two-pixel anchor checks.
Comment coloring remains correct after each transition.

The diff replay checks omitted prefixes in unstaged, staged, and commit reviews,
plus different comment states on the two split sides. It passes at all three
zooms. The final executable also passes six source/diff screenshot journeys with
color and visibility assertions. The source/string boundary and split-side
images were visually inspected.

Across the first 27 rolling-window and three diff benchmarks, p99 is
1.43–14.42 ms; the maximum frame is 24.68 ms. Every 20 ms p99 gate passes.
Three-page windows retain 307,178–307,224 raw bytes and 308,178–308,293 owned
window bytes, including cursor metadata. These figures exclude parsed text and
cache contents. Parsed-state vector capacity is included in the existing patch
cache and reading-probe accounting; budgets are unchanged.

The 213 targeted unit checks cover lexical delimiters, UTF-8/UTF-16, escaped
CRLF, direct/backward reads, cuts inside a delimiter, revision-specific seeds,
renames across languages, missing files, cancellation, stale publication,
source-window limits, token keys, review destinations, Git parsing, and context.
When a syntax prefix cannot be read, the existing hunk-local coloring remains;
no working-tree contents substitute for a missing historical revision.

Two unit fixtures retained references to the source buffer removed in step 49.
Those fixtures now use the bounded source window. The original compile failure
is retained in `output/step50-test_content_reader.log`; this is not a claim that
those two suites passed during step 49.

The final executable SHA-256 is
`4eb590dab625e3e06e41ac2d8b5f3294c64add2796bc63f34a2f269fa8e7a94d`.

The final build passes seven navigation regressions, nine wrapped-code/selection
journeys, and three local-context journeys covering working changes, commits,
and comparisons. The final diff replay also passes its three frame gates.
The full rolling-window matrix preceded the last one-line correction to use the
resolved commit scope; the source and diff journeys were replayed on the final
build after that correction.

Build failures for an include path and a namespace qualifier are retained beside
the corrected builds. No performance failure occurred in this step's native
feature replays. These tests do not establish physical live-resize latency or
resolve the separate 100 ms warm-switch target.

Evidence is packaged in `docs/reading-navigation-evidence/step50`. All 49 artifacts
(10,459,148 bytes), including seven selected PNGs, were checked for readability.
Native runs use hidden Cocoa/Metal windows; images come from separate offscreen
runs. The framework follow-up is recorded in `docs/afterhours-gaps.md`.
